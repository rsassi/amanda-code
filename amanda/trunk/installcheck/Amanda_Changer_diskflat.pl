# Copyright (c) 2008-2012 Zmanda, Inc.  All Rights Reserved.
# Copyright (c) 2013-2016 Carbonite, Inc.  All Rights Reserved.
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

use Test::More tests => 24;
use File::Path;
use strict;
use warnings;

use lib '@amperldir@';
use Installcheck;
use Installcheck::Config;
use Installcheck::Changer;
use Amanda::Paths;
use Amanda::Device qw( :constants );
use Amanda::Debug;
use Amanda::MainLoop;
use Amanda::Config qw( :init :getconf config_dir_relative );
use Amanda::Changer;
use Amanda::Tapelist;

# set up debugging so debug output doesn't interfere with test results
Amanda::Debug::dbopen("installcheck");
Installcheck::log_test_output();

# and disable Debug's die() and warn() overrides
Amanda::Debug::disable_die_override();

my $rtaperoot = "$Installcheck::TMP/Amanda_Changer_Diskflat_test";
my $taperoot = "$rtaperoot/flat";

sub reset_taperoot {
    my ($nslots) = @_;

    if (-d $taperoot) {
	rmtree($taperoot);
    }
    mkpath($taperoot);

    for my $slot (1 .. $nslots) {
	my $file = sprintf("$taperoot/AA-TESTCONF-%03s", $slot);
	open AA, ">$file"
	    or die("Could not open: $!");
	close AA;
    }
}

sub is_pointing_to {
    my ($res, $slot, $msg) = @_;

    my $label = sprintf("AA-TESTCONF-%03s", $slot);
    is($res->{'device'}->device_name, "diskflat:$taperoot/$label", $msg);

    # Should check the state file */
}

# Build a configuration that specifies Amanda::Changer::Disk
my $testconf = Installcheck::Config->new();
$testconf->add_changer('flat', [
	tpchanger => "\"chg-diskflat:$taperoot\"",
	property  => "\"num-slot\" \"5\"",
]);
$testconf->add_param('labelstr', 'MATCH-AUTOLABEL');
$testconf->add_param('meta-autolabel', '"!!"');
$testconf->add_param('autolabel', '"$m-TESTCONF-$3s" any');
$testconf->write();

my $cfg_result = config_init($CONFIG_INIT_EXPLICIT_NAME, 'TESTCONF');
if ($cfg_result != $CFGERR_OK) {
    my ($level, @errors) = Amanda::Config::config_errors();
    die(join "\n", @errors);
}

my $tlf = config_dir_relative("tapelist");
open AA, ">$tlf";
close AA;
my ($tl, $message) = Amanda::Tapelist->new($tlf);
reset_taperoot(5);

File::Path::rmtree($rtaperoot);
# first try an error
my $chg = Amanda::Changer->new("flat",
			       tapelist => $tl);
$chg = { %$chg }; #unbless
is_deeply(Installcheck::Config::remove_source_line($chg),
      { 'source_filename' => "$amperldir/Amanda/Changer/diskflat.pm",
	'process' => 'Amanda_Changer_diskflat',
	'running_on' => 'amanda-server',
	'component' => 'changer',
	'module' => 'Amanda::Changer::diskflat',
	'code' => 1100039,
	'severity' => $Amanda::Message::ERROR,
	'type' => 'fatal',
	'storage_name' => 'TESTCONF',
	'changer_message' => "directory '$taperoot' does not exist",
	'dir' => $taperoot,
	'message' => "Storage 'TESTCONF': directory '$taperoot' does not exist" },
    "detects nonexistent directory") or diag(Data::Dumper::Dumper($chg));

mkpath($rtaperoot);
$chg = Amanda::Changer->new("flat",
			    tapelist => $tl,
			    no_validate => 1);
rmtree($rtaperoot);
sub test_create {
    my ($finished_cb) = @_;
    my $steps = define_steps
        cb_ref => \$finished_cb;

    step one => sub {
	$chg->create(finished_cb => make_cb($finished_cb));
    }
}

test_create( sub { my ($err, $rv) = @_;
		   is($rv, undef, "create return undef");
		   $err = { %$err }; #unbless
		   is_deeply(Installcheck::Config::remove_source_line($err),
		      { 'source_filename' => "$amperldir/Amanda/Changer/diskflat.pm",
			'process' => 'Amanda_Changer_diskflat',
			'running_on' => 'amanda-server',
			'component' => 'changer',
			'module' => 'Amanda::Changer::diskflat',
			'type' => 'failed',
			'reason' => 'unknown',
			'code' => 1100026,
			'severity' => $Amanda::Message::ERROR,
			'storage_name' => 'TESTCONF',
			'chg_name' => 'flat',
			'dir' => $taperoot,
			'error' => 'No such file or directory',
			'changer_message' => "Can't create vtape root '$taperoot': No such file or directory",
			'message' => "Storage 'TESTCONF': Can't create vtape root '$taperoot': No such file or directory" },
		    "create succeed") or diag(Data::Dumper::Dumper($err));
		   Amanda::MainLoop::quit();
		 });


Amanda::MainLoop::run();
mkpath($rtaperoot);
die($chg) if $chg->isa("Amanda::Changer::Error");
test_create( sub { my ($err, $rv) = @_;
		   is($err, undef, "create return undef");
		   $rv = { %$rv }; #unbless
		   is_deeply(Installcheck::Config::remove_source_line($rv),
		      { 'source_filename' => "$amperldir/Amanda/Changer/diskflat.pm",
			'process' => 'Amanda_Changer_diskflat',
			'running_on' => 'amanda-server',
			'component' => 'changer',
			'module' => 'Amanda::Changer::diskflat',
			'code' => 1100027,
			'severity' => $Amanda::Message::SUCCESS,
			'storage_name' => 'TESTCONF',
			'chg_name' => 'flat',
			'dir' => $taperoot,
			'changer_message' => "Created vtape root '$taperoot'",
			'message' => "Storage 'TESTCONF': Created vtape root '$taperoot'" },
		    "create succeed") or diag(Data::Dumper::Dumper($rv));
		   Amanda::MainLoop::quit();
		 });
Amanda::MainLoop::run();

reset_taperoot(5);

File::Path::rmtree($taperoot);
# first try an error
$chg = Amanda::Changer->new("flat",
			       tapelist => $tl);
$chg = { %$chg }; #unbless
is_deeply(Installcheck::Config::remove_source_line($chg),
      { 'source_filename' => "$amperldir/Amanda/Changer/diskflat.pm",
	'process' => 'Amanda_Changer_diskflat',
	'running_on' => 'amanda-server',
	'component' => 'changer',
	'module' => 'Amanda::Changer::diskflat',
	'code' => 1100039,
	'severity' => $Amanda::Message::ERROR,
	'type' => 'fatal',
	'storage_name' => 'TESTCONF',
	'changer_message' => "directory '$taperoot' does not exist",
	'dir' => $taperoot,
	'message' => "Storage 'TESTCONF': directory '$taperoot' does not exist" },
    "detects nonexistent directory") or diag(Data::Dumper::Dumper($chg));

reset_taperoot(5);
$chg = Amanda::Changer->new("flat",
			    tapelist => $tl);
die($chg) if $chg->isa("Amanda::Changer::Error");
is($chg->have_inventory(), '1', "changer have inventory");

sub test_reserved {
    my ($finished_cb, @slots) = @_;
    my @reservations = ();
    my $slot;

    my $steps = define_steps
	cb_ref => \$finished_cb;

    step getres => sub {
	$slot = pop @slots;
	if (!defined $slot) {
	    return $steps->{'tryreserved'}->();
	}

	$chg->load(slot => $slot,
                   set_current => ($slot == 5),
		   res_cb => make_cb($steps->{'loaded'}));
    };

    step loaded => sub {
	my ($err, $reservation) = @_;
	ok(!$err, "no error loading slot $slot")
	    or diag($err);

	# keep this reservation
	if ($reservation) {
	    push @reservations, $reservation;
	}

	# and start on the next
	$steps->{'getres'}->();
    };

    step tryreserved => sub {
	# try to load an already-reserved slot
	$chg->load(slot => 3,
		   res_cb => sub {
	    my ($err, $reservation) = @_;
	    $err = { %$err }; #unbless
	    my $pid = $err->{'pid'};
	    my $drive = $err->{'drive'};
	    is_deeply(Installcheck::Config::remove_source_line($err),
	      { 'source_filename' => "$amperldir/Amanda/Changer/diskflat.pm",
		'process' => 'Amanda_Changer_diskflat',
		'running_on' => 'amanda-server',
		'component' => 'changer',
		'module' => 'Amanda::Changer::diskflat',
		'code' => 1100034,
		'severity' => $Amanda::Message::ERROR,
		'type' => 'failed',
		'reason'=> 'volinuse',
		'storage_name' => 'TESTCONF',
		'slot' => 3,
		'drive' => $drive,
		'pid'=> $pid,
		'changer_message' => "Slot 3 is already in use by drive '$drive' and process '$pid'",
		'message' => "Storage 'TESTCONF': Slot 3 is already in use by drive '$drive' and process '$pid'" },
		"error when requesting already-reserved slot") || diag(Data::Dumper::Dumper($err));
	    $steps->{'release'}->();
	});
    };

    step release => sub {
	my $res = pop @reservations;
	if (!defined $res) {
	    return Amanda::MainLoop::quit();
	}

	$res->release(finished_cb => sub {
	    my ($err) = @_;
	    die $err if $err;
	    $steps->{'release'}->();
	});
    };
}

# start the loop
test_reserved(\&Amanda::MainLoop::quit, 1, 3, 5);
Amanda::MainLoop::run();

# and try it with some different slots, just to see
test_reserved(\&Amanda::MainLoop::quit, 4, 2, 3);
Amanda::MainLoop::run();

# check relative slot ("current" and "next") functionality
sub test_relative_slot {
    my ($finished_cb) = @_;
    my $slot;

    my $steps = define_steps
	cb_ref => \$finished_cb;

    # load the "current" slot, which should be 3
    step load_current => sub {
	$chg->load(relative_slot => "current", res_cb => $steps->{'check_current_cb'});
    };

    step check_current_cb => sub {
        my ($err, $res) = @_;
        die $err if $err;

        is_pointing_to($res, 5, "'current' is slot 5");
	$slot = $res->{'this_slot'};

	$res->release(finished_cb => $steps->{'released1'});
    };

    step released1 => sub {
	my ($err) = @_;
	die $err if $err;

        $chg->load(relative_slot => 'next', slot => $slot,
		   res_cb => $steps->{'check_next_cb'});
    };

    step check_next_cb => sub {
        my ($err, $res) = @_;
        die $err if $err;

        is_pointing_to($res, 1, "'next' from there is slot 1");

	$res->release(finished_cb => $steps->{'released2'});
    };

    step released2 => sub {
	my ($err) = @_;
	die $err if $err;

        $chg->reset(finished_cb => $steps->{'reset_finished_cb'});
    };

    step reset_finished_cb => sub {
        my ($err) = @_;
        die $err if $err;

	$chg->load(relative_slot => "current", res_cb => $steps->{'check_reset_cb'});
    };

    step check_reset_cb => sub {
        my ($err, $res) = @_;
        die $err if $err;

        is_pointing_to($res, 1, "after reset, 'current' is slot 1");

	$res->release(finished_cb => $steps->{'released3'});
    };

    step released3 => sub {
	my ($err) = @_;
	die $err if $err;

        $finished_cb->();
    };
}
test_relative_slot(\&Amanda::MainLoop::quit);
Amanda::MainLoop::run();

# test loading relative_slot "next"
sub test_relative_next {
    my ($finished_cb) = @_;

    my $steps = define_steps
	cb_ref => \$finished_cb;

    step load_next => sub {
        $chg->load(relative_slot => "next",
            res_cb => sub {
                my ($err, $res) = @_;
                die $err if $err;

                is_pointing_to($res, 2, "loading relative slot 'next' loads the correct slot");

		$steps->{'release'}->($res);
            }
        );
    };

    step release => sub {
	my ($res) = @_;

	$res->release(finished_cb => sub {
	    my ($err) = @_;
	    die $err if $err;

	    $finished_cb->();
	});
    };
}
test_relative_next(\&Amanda::MainLoop::quit);
Amanda::MainLoop::run();

# scan the changer using except_slots
sub test_except_slots {
    my ($finished_cb) = @_;
    my $slot;
    my %except_slots;

    my $steps = define_steps
	cb_ref => \$finished_cb;

    step start => sub {
	$chg->load(slot => "5", except_slots => { %except_slots },
		   res_cb => $steps->{'loaded'});
    };

    step loaded => sub {
        my ($err, $res) = @_;
	if ($err) {
	    if ($err->notfound) {
		# this means the scan is done
		return $steps->{'quit'}->();
	    } elsif ($err->volinuse and defined $err->{'slot'}) {
		$slot = $err->{'slot'};
	    } else {
		die $err;
	    }
	} else {
	    $slot = $res->{'this_slot'};
	}

	$except_slots{$slot} = 1;

	if ($res) {
	    $res->release(finished_cb => $steps->{'released'});
	} else {
	    $steps->{'released'}->();
	}
    };

    step released => sub {
	my ($err) = @_;
	die $err if $err;

        $chg->load(relative_slot => 'next', slot => $slot,
		   except_slots => { %except_slots },
		   res_cb => $steps->{'loaded'});
    };

    step quit => sub {
	is_deeply({ %except_slots }, { 5=>1, 1=>1, 2=>1, 3=>1, 4=>1 },
		"scanning with except_slots works");
	$finished_cb->();
    };
}
test_except_slots(\&Amanda::MainLoop::quit);
Amanda::MainLoop::run();

# eject is not implemented
{
    my $try_eject = make_cb('try_eject' => sub {
        $chg->eject(finished_cb => make_cb(sub {
	    my ($err, $res) = @_;
	    $err = { %$err }; #unbless
	    is_deeply(Installcheck::Config::remove_source_line($err),
	      { 'source_filename' => "$amperldir/Amanda/Changer.pm",
		'process' => 'Amanda_Changer_diskflat',
		'running_on' => 'amanda-server',
		'component' => 'changer',
		'module' => 'Amanda::Changer::diskflat',
		'code' => 1100048,
		'severity' => $Amanda::Message::ERROR,
		'type' => 'failed',
		'reason'=> 'notimpl',
		'storage_name' => 'TESTCONF',
		'chg_type' => 'chg-diskflat',
		'chg_name' => 'flat',
		'op' => 'eject',
		'changer_message' => '\'chg-diskflat\' does not support eject',
		'message' => 'Storage \'TESTCONF\': \'chg-diskflat\' does not support eject' },
		"eject returns a failed/notimpl error") || diag(Data::Dumper::Dumper($err));

	    Amanda::MainLoop::quit();
	}));
    });

    $try_eject->();
    Amanda::MainLoop::run();
}

# check num_slots and loading by label
{
    my ($get_info, $load_label, $check_load_cb) = @_;

    $get_info = make_cb('get_info' => sub {
        $chg->info(info_cb => $load_label, info => [ 'num_slots', 'fast_search' , 'slots', 'vendor_string']);
    });

    $load_label = make_cb('load_label' => sub {
        my $err = shift;
        my %results = @_;
        die($err) if defined($err);

        is_deeply({ %results },
	    { num_slots => 5,
	      fast_search => 1,
	      vendor_string => 'chg-diskflat',
	      slots => [ '1', '2', '3', '4', '5' ] },
	    "info() returns the correct num_slots and fast_search") ||
	    diag("result: " . Data::Dumper::Dumper(\%results));

        # note use of a glob metacharacter in the label name
        $chg->load(label => "AA-TESTCONF-004", res_cb => $check_load_cb);
    });

    $check_load_cb = make_cb('check_load_cb' => sub {
        my ($err, $res) = @_;
        die $err if $err;

        is_pointing_to($res, 4, "labeled volume found in slot 4");

	$res->release(finished_cb => sub {
	    my ($err) = @_;
	    die $err if $err;

	    Amanda::MainLoop::quit();
	});
    });

    # label slot 4, using our own symlink
    my $dev = Amanda::Device->new("diskflat:$taperoot/AA-TESTCONF-004");
    $dev->start($Amanda::Device::ACCESS_WRITE, "AA-TESTCONF-004", undef)
        or die $dev->error_or_status();
    $dev->finish()
        or die $dev->error_or_status();
    rmtree("$taperoot/tmp");

    $get_info->();
    Amanda::MainLoop::run();
}

# inventory is pretty cool
{
    my $try_inventory = make_cb('try_inventory' => sub {
        $chg->inventory(inventory_cb => make_cb(sub {
	    my ($err, $inv) = @_;
	    die $err if $err;

	    is_deeply($inv, [
	      { slot => 1, state => Amanda::Changer::SLOT_FULL,
		device_status => $DEVICE_STATUS_VOLUME_UNLABELED,
		f_type => $Amanda::Header::F_EMPTY, label => undef,
		reserved => 0,  current => 1},
	      { slot => 2, state => Amanda::Changer::SLOT_FULL,
		device_status => $DEVICE_STATUS_VOLUME_UNLABELED,
		f_type => $Amanda::Header::F_EMPTY, label => undef,
		reserved => 0 },
	      { slot => 3, state => Amanda::Changer::SLOT_FULL,
		device_status => $DEVICE_STATUS_VOLUME_UNLABELED,
		f_type => $Amanda::Header::F_EMPTY, label => undef,
		reserved => 0 },
	      { slot => 4, state => Amanda::Changer::SLOT_FULL,
		device_status => $DEVICE_STATUS_SUCCESS,
		f_type => $Amanda::Header::F_TAPESTART, label => "AA-TESTCONF-004",
		reserved => 0 },
	      { slot => 5, state => Amanda::Changer::SLOT_FULL,
		device_status => $DEVICE_STATUS_VOLUME_UNLABELED,
		f_type => $Amanda::Header::F_EMPTY, label => undef,
		reserved => 0 },
		], "inventory finds the labeled tape");

	    Amanda::MainLoop::quit();
	}));
    });

    $try_inventory->();
    Amanda::MainLoop::run();
}

$chg->quit();
rmtree($taperoot);
