# Copyright (c) 2010-2012 Zmanda Inc.  All Rights Reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
#
# Contact information: Carbonite Inc., 756 N Pastoria Ave
# Sunnyvale, CA 94086, USA, or: http://www.zmanda.com

use Test::More tests => 12;
use File::Path;
use Data::Dumper;
use strict;
use warnings;

use lib '@amperldir@';
use Installcheck;
use Installcheck::Config;
use Installcheck::Changer;
use Installcheck::Mock qw( setup_mock_mtx $mock_mtx_path );
use Installcheck::DBCatalog2;
use Amanda::Device qw( :constants );
use Amanda::Debug;
use Amanda::MainLoop;
use Amanda::Config qw( :init :getconf config_dir_relative );
use Amanda::Storage;
use Amanda::Changer;
use Amanda::Taper::Scan;
use Amanda::DB::Catalog2;

# set up debugging so debug output doesn't interfere with test results
Amanda::Debug::dbopen("installcheck");
Installcheck::log_test_output();

# and disable Debug's die() and warn() overrides
Amanda::Debug::disable_die_override();

my $taperoot = "$Installcheck::TMP/Amanda_Taper_Scan_traditional";
my $tapelist_filename = "$Installcheck::TMP/tapelist";

# vtape support
my %slot_label;

my $catalog;
sub reset_taperoot {
    my ($nslots) = @_;

    $catalog->quit() if defined $catalog;

    if (-d $taperoot) {
	rmtree($taperoot);
    }
    mkpath($taperoot);

    for my $slot (1 .. $nslots) {
	mkdir("$taperoot/slot$slot")
	    or die("Could not mkdir: $!");
    }

    # clear out the tapefile
    open(my $fh, ">", $tapelist_filename) or die("opening tapelist_filename: $!");
    close($fh);
    %slot_label = ();
    my $catalog_name = getconf($CNF_CATALOG);
    my $catalog_conf = lookup_catalog($catalog_name);
    $catalog = Amanda::DB::Catalog2->new($catalog_conf, config_name => 'TESTCONF', create => 1, drop_tables => 1, load => 1);
}

sub label_slot {
    my ($slot, $label, $stamp, $reuse, $storage, $update_tapelist) = @_;

    my $drivedir = "$taperoot/tmp";
    -d $drivedir and rmtree($drivedir);
    mkpath($drivedir);
    symlink("$taperoot/slot$slot", "$drivedir/data");

    my $dev = Amanda::Device->new("file:$drivedir");
    die $dev->error_or_status() unless $dev->status == $DEVICE_STATUS_SUCCESS;

    if (defined $label){
	if (!$dev->start($ACCESS_WRITE, $label, $stamp)) {
	    die $dev->error_or_status();
	}
    } else {
	$dev->erase();
    }

    rmtree($drivedir);

    if ($update_tapelist) {
	if (exists $slot_label{$slot}) {
	    $catalog->remove_volume("TESTCONF", $slot_label{$slot});
	    delete $slot_label{$slot};
	}
	# tapelist uses '0' for new tapes; devices use 'X'..
	$stamp = '0' if ($stamp eq 'X');
	$reuse = $reuse ne 'no-reuse';
	if (defined $label) {
	    $slot_label{$slot} = $label;
	} else {
	}
	$catalog->remove_volume("TESTCONF", $label);
	$catalog->add_volume("TESTCONF", $label, $stamp, $storage, undef, undef, undef, $reuse, 0, 0, 0, $stamp!=0);
    }
}

sub label_mtx_slot {
    my ($slot, $label, $stamp, $reuse, $storage, $update_tapelist) = @_;

    my $drivedir = "$taperoot/tmp";
    -d $drivedir and rmtree($drivedir);
    mkpath($drivedir);
    mkdir("$taperoot/slot$slot/data");
    symlink("$taperoot/slot$slot/data", "$drivedir/data");

    my $dev = Amanda::Device->new("file:$drivedir");
    die $dev->error_or_status() unless $dev->status == $DEVICE_STATUS_SUCCESS;

    if (defined $label){
	if (!$dev->start($ACCESS_WRITE, $label, $stamp)) {
	    die $dev->error_or_status();
	}
    } else {
	$dev->erase();
    }

    rmtree($drivedir);

    if ($update_tapelist) {
	if (exists $slot_label{$slot}) {
	    $catalog->remove_volume("TESTCONF", $slot_label{$slot});
	    delete $slot_label{$slot};
	}
	# tapelist uses '0' for new tapes; devices use 'X'..
	$stamp = '0' if ($stamp eq 'X');
	$reuse = $reuse ne 'no-reuse';
	$slot_label{$slot} = $label;
	$catalog->remove_volume("TESTCONF", $label);
	$catalog->add_volume("TESTCONF", $label, $stamp, $storage, undef, undef, undef, $reuse, 0, 0, 0, $stamp!=01);
    }
}

# run the mainloop around a scan
sub run_scan {
    my ($ts) = @_;
    my ($error, $res, $label, $mode);

    my $result_cb = make_cb(result_cb => sub {
	($error, $res, $label, $mode) = @_;

	if ($res) {
	    $res->release(finished_cb => sub {
		Amanda::MainLoop::quit();
	    });
	} else {
	    Amanda::MainLoop::quit();
	}
    });

    $ts->scan(result_cb => $result_cb);
    Amanda::MainLoop::run();
    return $error, $label, $mode;
}

# set the current slot on the changer
sub set_current_slot {
    my ($chg, $slot) = @_;

    $chg->load(slot => $slot, set_current => 1,
	res_cb => sub {
	    my ($err, $reservation) = @_;
            if ($err) {
                Amanda::MainLoop::quit();
		return;
            }
            $reservation->release(
                finished_cb => sub {
                     Amanda::MainLoop::quit();
		     return;
                });

	});
    Amanda::MainLoop::run();
}

# set up and load a config
my $testconf = Installcheck::Config->new();
$testconf->add_param("tapelist", "\"$tapelist_filename\"");
$testconf->add_policy("test_policy", [ retention_tapes => 4 ]);
$testconf->add_storage("disk", [ tpchanger => "\"chg-disk:$taperoot\"",
				 policy    => "\"test_policy\"",
				 labelstr  => "\"TEST-[0-9]+\"" ]);
$testconf->add_param("storage", "\"disk\"");
$testconf->write( do_catalog => 0 );
my $cfg_result = config_init($CONFIG_INIT_EXPLICIT_NAME, 'TESTCONF');
if ($cfg_result != $CFGERR_OK) {
    my ($level, @errors) = Amanda::Config::config_errors();
    die(join "\n", @errors);
}

reset_taperoot(5);
label_slot(1, "TEST-1", "20090424173001", "reuse", "disk", 1);
label_slot(2, "TEST-2", "20090424173002", "reuse", "disk", 1);
label_slot(3, "TEST-3", "20090424173003", "reuse", "disk", 1);
$catalog->add_simple_dump("localhost","/boot","/boot", 20090424173001, 0, "TESTCONF", "disk", "TEST-1", 1, 0, 0, 0);
$catalog->add_simple_dump("localhost","/boot","/boot", 20090424173002, 0, "TESTCONF", "disk", "TEST-2", 1, 0, 0, 0);
$catalog->add_simple_dump("localhost","/boot","/boot", 20090424173003, 0, "TESTCONF", "disk", "TEST-3", 1, 0, 0, 0);
$catalog->compute_retention();

my $storage;
my $chg;
my $taperscan;
my @results;

# set up a traditional taperscan
$storage = Amanda::Storage->new(storage_name => "disk", catalog => $catalog);
$taperscan = Amanda::Taper::Scan->new(
    catalog => $catalog,
    algorithm => "traditional",
    storage => $storage);
@results = run_scan($taperscan);
is_deeply([ @results ],
	  [ "Storage 'disk': No acceptable volumes found", undef, undef ],
	  "no reusable tapes -> error")
	  or diag(Dumper(\@results));
$taperscan->quit();
$storage->quit();
$catalog->quit();

$testconf->add_policy("test_policy", [ retention_tapes => 2 ]);
$testconf->write( do_catalog => 0 );
$cfg_result = config_init($CONFIG_INIT_EXPLICIT_NAME, 'TESTCONF');
if ($cfg_result != $CFGERR_OK) {
    my ($level, @errors) = Amanda::Config::config_errors();
    die(join "\n", @errors);
}
$catalog = Amanda::DB::Catalog2->new();
$catalog->compute_retention();

$storage = Amanda::Storage->new(storage_name => "disk", catalog => $catalog);
$taperscan = Amanda::Taper::Scan->new(
    catalog => $catalog,
    algorithm => "traditional",
    storage => $storage);
@results = run_scan($taperscan);
is_deeply([ @results ],
	  [ undef, "TEST-1", $ACCESS_WRITE ],
	  "finds the best reusable tape")
	  or diag(Dumper(\@results));
$taperscan->quit();
$storage->quit();
$catalog->quit();

$testconf->add_policy("test_policy", [ retention_tapes => 1 ]);
$testconf->write( do_catalog => 0 );
$cfg_result = config_init($CONFIG_INIT_EXPLICIT_NAME, 'TESTCONF');
if ($cfg_result != $CFGERR_OK) {
    my ($level, @errors) = Amanda::Config::config_errors();
    die(join "\n", @errors);
}
$catalog = Amanda::DB::Catalog2->new();
$catalog->compute_retention();

$storage = Amanda::Storage->new(storage_name => "disk", catalog => $catalog);
die "$storage" if $storage->isa("Amanda::Changer::Error");
$chg = $storage->{'chg'};
$chg->{'support_fast_search'} = 0; # no fast search -> skip stage 1
set_current_slot($chg, 2); # slot 2 is acceptable, so it should be returned
$taperscan = Amanda::Taper::Scan->new(
    catalog => $catalog,
    algorithm => "traditional",
    storage => $storage);
@results = run_scan($taperscan);
is_deeply([ @results ],
	  [ undef, "TEST-2", $ACCESS_WRITE ],
	  "finds the first reusable tape when fast_search is false")
	  or diag(Dumper(\@results));
$taperscan->quit();
$storage->quit();
$catalog->quit();

$testconf->add_policy("test_policy", [ retention_tapes => 1 ]);
$testconf->write( do_catalog => 0 );
$cfg_result = config_init($CONFIG_INIT_EXPLICIT_NAME, 'TESTCONF');
if ($cfg_result != $CFGERR_OK) {
    my ($level, @errors) = Amanda::Config::config_errors();
    die(join "\n", @errors);
}
$catalog = Amanda::DB::Catalog2->new();
$catalog->compute_retention();

$storage = Amanda::Storage->new(storage_name => "disk", catalog => $catalog);
$chg = $storage->{'chg'};
$chg->{'support_fast_search'} = 1;

label_slot(1, undef, undef, undef, "disk"); # remove TEST-1
label_slot(4, "TEST-4", "20090424183004", "reuse", "disk", 1);
set_current_slot($chg, 1);
$taperscan = Amanda::Taper::Scan->new(
    catalog => $catalog,
    algorithm => "traditional",
    storage => $storage);
@results = run_scan($taperscan);
is_deeply([ @results ],
	  [ undef, "TEST-2", $ACCESS_WRITE ],
	  "uses the first usable tape it finds when oldest is missing")
	  or diag(Dumper(\@results));
$taperscan->quit();
$storage->quit();

$catalog->compute_retention();
$storage = Amanda::Storage->new(storage_name => "disk", catalog => $catalog);
$chg = $storage->{'chg'};
$chg->{'support_fast_search'} = 1;

set_current_slot($chg, 3);
$taperscan = Amanda::Taper::Scan->new(
    catalog => $catalog,
    algorithm => "traditional",
    retention_tapes => 1,
    storage => $storage);
@results = run_scan($taperscan);
is_deeply([ @results ],
	  [ undef, "TEST-3", $ACCESS_WRITE ],
	  "starts sequential scan at 'current'")
	  or diag(Dumper(\@results));
$taperscan->quit();
$storage->quit();
$catalog->quit();

$testconf->add_policy("test_policy", [ retention_tapes => 2 ]);
$testconf->add_storage("disk", [ tpchanger => "\"chg-disk:$taperoot\"",
				 policy    => "\"test_policy\"",
				 labelstr  => "\"TEST-[0-9]+\"",
				 autolabel  => "\"TEST-%\" empty volume-error" ]);
$testconf->write( do_catalog => 0 );
$cfg_result = config_init($CONFIG_INIT_EXPLICIT_NAME, 'TESTCONF');
if ($cfg_result != $CFGERR_OK) {
    my ($level, @errors) = Amanda::Config::config_errors();
    die(join "\n", @errors);
}
$catalog = Amanda::DB::Catalog2->new();
$catalog->compute_retention();

$storage = Amanda::Storage->new(storage_name => "disk", catalog => $catalog);
$chg = $storage->{'chg'};
$taperscan = Amanda::Taper::Scan->new(
    catalog => $catalog,
    algorithm => "traditional",
    retention_tapes => 2,
    storage => $storage);
set_current_slot($chg, 5);
@results = run_scan($taperscan);
is_deeply([ @results ],
	  [ undef, "TEST-5", $ACCESS_WRITE ],
	  "labels new tapes in blank slots")
	  or diag(Dumper(\@results));
$taperscan->quit();
$storage->quit();
$catalog->quit();

$testconf->add_policy("test_policy", [ retention_tapes => 1 ]);
$testconf->add_storage("disk", [ tpchanger => "\"chg-disk:$taperoot\"",
				 policy    => "\"test_policy\"",
				 labelstr  => "\"TEST-[0-9]+\"" ]);
$testconf->write( do_catalog => 0 );
$cfg_result = config_init($CONFIG_INIT_EXPLICIT_NAME, 'TESTCONF');
if ($cfg_result != $CFGERR_OK) {
    my ($level, @errors) = Amanda::Config::config_errors();
    die(join "\n", @errors);
}
$catalog = Amanda::DB::Catalog2->new();
$catalog->compute_retention();

$storage = Amanda::Storage->new(storage_name => "disk", catalog => $catalog);
$chg = $storage->{'chg'};
$taperscan = Amanda::Taper::Scan->new(
    catalog => $catalog,
    algorithm => "traditional",
    storage => $storage);
set_current_slot($chg, 6);
@results = run_scan($taperscan);
is_deeply([ @results ],
	  [ undef, "TEST-2", $ACCESS_WRITE ],
	  "handles an invalid current slot by going to the next")
	  or diag(Dumper(\@results));
$taperscan->quit();
$storage->quit();
$catalog->quit();
$testconf->add_policy("test_policy", [ retention_tapes => 2 ]);
$testconf->add_storage("disk", [ tpchanger => "\"chg-disk:$taperoot\"",
				 policy    => "\"test_policy\"" ]);
$testconf->write( do_catalog => 0 );
$catalog = Amanda::DB::Catalog2->new();
$catalog->compute_retention();

$storage = Amanda::Storage->new(storage_name => "disk", catalog => $catalog);
$chg = $storage->{'chg'};
label_slot(1, "TEST-6", "X", "reuse", "disk", 1);
$taperscan = Amanda::Taper::Scan->new(
    catalog => $catalog,
    algorithm => "traditional",
    storage => $storage);
set_current_slot($chg, 2);
@results = run_scan($taperscan);
is_deeply([ @results ],
	  [ undef, "TEST-2", $ACCESS_WRITE ],
	  "scans for volumes, even with a newly labeled volume available")
	  or diag(Dumper(\@results));
$taperscan->quit();
$storage->quit();

# test skipping no-reuse tapes
reset_taperoot(5);
label_slot(1, "TEST-1", "20090424173001", "no-reuse", "disk", 1);
label_slot(2, "TEST-2", "20090424173002", "reuse", "disk", 1);
label_slot(3, "TEST-3", "20090424173003", "reuse", "disk", 1);
label_slot(4, "TEST-4", "20090424173004", "reuse", "disk", 1);
$catalog->quit();

$testconf->add_policy("test_policy", [ retention_tapes => 2 ]);
$testconf->add_storage("disk", [ tpchanger => "\"chg-disk:$taperoot\"",
				 policy    => "\"test_policy\"",
				 labelstr  => "\"TEST-[0-9]+\"" ]);
$testconf->write( do_catalog => 0 );
$cfg_result = config_init($CONFIG_INIT_EXPLICIT_NAME, 'TESTCONF');
if ($cfg_result != $CFGERR_OK) {
    my ($level, @errors) = Amanda::Config::config_errors();
    die(join "\n", @errors);
}
$catalog = Amanda::DB::Catalog2->new();
$catalog->add_simple_dump("localhost","/boot","/boot", 20090424173001, 0, "TESTCONF", "disk", "TEST-1", 1, 0, 0, 0);
$catalog->add_simple_dump("localhost","/boot","/boot", 20090424173002, 0, "TESTCONF", "disk", "TEST-2", 1, 0, 0, 0);
$catalog->add_simple_dump("localhost","/boot","/boot", 20090424173003, 0, "TESTCONF", "disk", "TEST-3", 1, 0, 0, 0);
$catalog->add_simple_dump("localhost","/boot","/boot", 20090424173004, 0, "TESTCONF", "disk", "TEST-4", 1, 0, 0, 0);
$catalog->compute_retention();

$storage = Amanda::Storage->new(storage_name => "disk", catalog => $catalog);
$chg = $storage->{'chg'};
#$chg = Amanda::Changer->new("chg-disk:$taperoot");
set_current_slot($chg, 1);

$taperscan = Amanda::Taper::Scan->new(
    catalog => $catalog,
    algorithm => "traditional",
    storage => $storage);
@results = run_scan($taperscan);
is_deeply([ @results ],
	  [ undef, "TEST-2", $ACCESS_WRITE ],
	  "skips a no-reuse volume")
	  or diag(Dumper(\@results));
$taperscan->quit();
$storage->quit();

rmtree($taperoot);
unlink($tapelist_filename);

# test do not use no-reuse with a datestamp of 0
reset_taperoot(5);

label_slot(1, "TEST-1", "X", "no-reuse", "disk", 1);
label_slot(2, "TEST-2", "X", "no-reuse", "disk", 1);
label_slot(3, "TEST-3", "X", "reuse", "disk", 1);
label_slot(4, "TEST-4", "X", "no-reuse", "disk", 1);
label_slot(4, "TEST-5", "X", "no-reuse", "disk", 1);
$catalog->compute_retention();

$storage = Amanda::Storage->new(storage_name => "disk", catalog => $catalog);
$chg = $storage->{'chg'};
set_current_slot($chg, 1);

$taperscan = Amanda::Taper::Scan->new(
    catalog => $catalog,
    algorithm => "traditional",
    tapecycle => 2,
    tapecycle => 1,
    storage => $storage);
@results = run_scan($taperscan);
is_deeply([ @results ],
         [ undef, "TEST-3", $ACCESS_WRITE ],
         "skips a no-reuse volume")
         or diag(Dumper(\@results));
$taperscan->quit();
$storage->quit();

rmtree($taperoot);
unlink($tapelist_filename);

# test invalid because slot loaded in invalid drive
my $chg_state_file = "$Installcheck::TMP/chg-robot-state";
unlink($chg_state_file) if -f $chg_state_file;
my $vtape_root = $taperoot;
my $mtx_state_file = setup_mock_mtx (
	 num_slots => 5,
	 num_ie => 0,
	 barcodes => 1,
	 track_orig => 1,
	 num_drives => 2,
	 vtape_root => $vtape_root,
	 loaded_slots => {
		1 => '11111',
		2 => '22222',
		3 => '33333',
		4 => '44444',
		5 => '55555',
	  },
	 first_slot => 1,
	 first_drive => 0,
	 first_ie => 6,
	);

$testconf = Installcheck::Config->new();
$testconf->add_param("tapelist", "\"$tapelist_filename\"");
$testconf->add_param("labelstr", "\"TEST-[0-9]+\"");
$testconf->add_changer('robo1', [
	tpchanger => "\"chg-robot:$mtx_state_file\"",
	changerfile => "\"$chg_state_file\"",

	# point to the two vtape "drives" that mock/mtx will set up
	property => "\"tape-device\" \"1=file:$vtape_root/drive1\"",

	# an point to the mock mtx
	property => "\"mtx\" \"$mock_mtx_path\"",
]);
$testconf->add_changer('robo2', [
	tpchanger => "\"chg-robot:$mtx_state_file\"",
	changerfile => "\"$chg_state_file\"",

	# point to the two vtape "drives" that mock/mtx will set up
	property => "\"tape-device\" \"0=file:$vtape_root/drive0\" \"1=file:$vtape_root/drive1\"",

	# an point to the mock mtx
	property => "\"mtx\" \"$mock_mtx_path\"",
]);
$testconf->add_policy("test_policy", [ retention_tapes => 2 ]);
$testconf->add_storage("robo1", [ tpchanger => "\"robo1\"",
				  policy => "\"test_policy\"",
				 labelstr  => "\"TEST-[0-9]+\"" ]);
$testconf->add_storage("robo2", [ tpchanger => "\"robo2\"",
				  policy => "\"test_policy\"",
				 labelstr  => "\"TEST-[0-9]+\"" ]);
$testconf->write(do_catalog => 0);
reset_taperoot(5);
$cfg_result = config_init($CONFIG_INIT_EXPLICIT_NAME, 'TESTCONF');
if ($cfg_result != $CFGERR_OK) {
	my ($level, @errors) = Amanda::Config::config_errors();
	die(join "\n", @errors);
}

label_mtx_slot(1, "TEST-1", "20090424173001", "reuse", "robo1", 1);
label_mtx_slot(2, "TEST-2", "20090424173002", "reuse", "robo1", 1);
label_mtx_slot(3, "TEST-3", "20090424173003", "reuse", "robo1", 1);
label_mtx_slot(4, "TEST-4", "20090424173004", "reuse", "robo1", 1);
$catalog->quit();

$testconf->remove_param("tpchanger");
$testconf->add_param("storage", "\"robo2\"");
$testconf->write( do_catalog => 0 );
$cfg_result = config_init($CONFIG_INIT_EXPLICIT_NAME, 'TESTCONF');
if ($cfg_result != $CFGERR_OK) {
	my ($level, @errors) = Amanda::Config::config_errors();
	die(join "\n", @errors);
}
$catalog = Amanda::DB::Catalog2->new();
$catalog->compute_retention();

$storage = Amanda::Storage->new(storage_name => "robo2",
				catalog => $catalog);
$chg = $storage->{'chg'};
die "$chg" if $chg->isa("Amanda::Changer::Error");

sub test_robot {
    my $finished_cb = shift;

    my $steps = define_steps
        cb_ref => \$finished_cb;

    step setup => sub {
        $chg->load(slot => 1, res_cb => $steps->{'loaded_slot'});
    };

    step loaded_slot => sub {
        my ($err, $res1) = @_;
        die $err if $err;

        is($res1->{'device'}->device_name, "file:$vtape_root/drive0",
            "first load returns drive-0 device");

        $res1->release(finished_cb => $steps->{'released'});
    };

    step released => sub {
        $chg->quit();
	$storage = Amanda::Storage->new(storage_name => "robo1",
					catalog => $catalog);
	$chg = $storage->{'chg'};
        $taperscan = Amanda::Taper::Scan->new(
	    catalog => $catalog,
	    algorithm => "traditional",
	    tapecycle => 2,
	    storage => $storage);
        @results = run_scan($taperscan);
        is_deeply([ @results ],
	    [ undef, "TEST-2", $ACCESS_WRITE ],
	      "skip a slot loaded in an inaccessible drive")
	      or diag(Dumper(\@results));
        $chg->quit();
        $taperscan->quit();
	$storage->quit();
    };

};

test_robot(\&Amanda::MainLoop::quit);
Amanda::MainLoop::run();
$catalog->quit();

rmtree($taperoot);
unlink($tapelist_filename);

unlink($chg_state_file) if -f $chg_state_file;
unlink($mtx_state_file) if -f $mtx_state_file;
rmtree($vtape_root);
unlink($tapelist_filename);
