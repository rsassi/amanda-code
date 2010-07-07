# Copyright (c) 2009, 2010 Zmanda Inc.  All Rights Reserved.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published
# by the Free Software Foundation.
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
# Contact information: Zmanda Inc, 465 S. Mathilda Ave., Suite 300
# Sunnyvale, CA 94086, USA, or: http://www.zmanda.com

use Test::More tests => 36;
use File::Path;
use Data::Dumper;
use strict;

use lib "@amperldir@";
use Installcheck::Config;
use Amanda::Config qw( :init );
use Amanda::Changer;
use Amanda::Device qw( :constants );
use Amanda::Debug;
use Amanda::Header;
use Amanda::Xfer;
use Amanda::Taper::Scribe qw( get_splitting_args_from_config );
use Amanda::MainLoop;

# and disable Debug's die() and warn() overrides
Amanda::Debug::disable_die_override();

# put the debug messages somewhere
Amanda::Debug::dbopen("installcheck");
Installcheck::log_test_output();

# use some very small vtapes
my $volume_length = 512*1024;

my $testconf;
$testconf = Installcheck::Config->new();
$testconf->add_tapetype("TEST-TAPE", [
    "length" => ($volume_length / 1024) . " k",
]);
$testconf->write();

my $cfg_result = config_init($CONFIG_INIT_EXPLICIT_NAME, 'TESTCONF');
if ($cfg_result != $CFGERR_OK) {
    my ($level, @errors) = Amanda::Config::config_errors();
    die(join "\n", @errors);
}

my $taperoot = "$Installcheck::TMP/Amanda_Taper_Scribe";

sub reset_taperoot {
    my ($nslots) = @_;

    if (-d $taperoot) {
	rmtree($taperoot);
    }
    mkpath($taperoot);

    for my $slot (1 .. $nslots) {
	mkdir("$taperoot/slot$slot")
	    or die("Could not mkdir: $!");
    }
}

# an accumulator for the sequence of events that transpire during a run
our @events;
sub event(@) {
    my $evt = [ @_ ];
    push @events, $evt;
}

sub reset_events {
    @events = ();
}

# construct a bigint
sub bi {
    Math::BigInt->new($_[0]);
}

# and similarly an Amanda::Changer::Error
sub chgerr {
    Amanda::Changer::Error->new(@_);
}

##
## Mock classes for the scribe
##

package Mock::Taperscan;
use Amanda::Device qw( :constants );
use Amanda::MainLoop;

sub new {
    my $class = shift;
    my @slots = @_;
    my $chg =  Amanda::Changer->new("chg-disk:$taperoot");
    die $chg if $chg->isa("Amanda::Changer::Error");

    return bless {
	chg => $chg,
	slots => [ @slots ],
	next_or_current => "current",
    }, $class;
}

sub scan {
    my $self = shift;
    my %params = @_;
    my $result_cb = $params{'result_cb'};

    main::event("scan");

    my @slotarg = (@{$self->{'slots'}})?
	  (slot => shift @{$self->{'slots'}})
	: (relative_slot => $self->{'next_or_current'});
    $self->{'next_or_current'} = 'next';

    my $res_cb = make_cb('res_cb' => sub {
	my ($err, $res) = @_;

	my $slot = $res? $res->{'this_slot'} : "none";
	main::event("scan-finished", main::undef_or_str($err), "slot: $slot");

	if ($err) {
	    $result_cb->($err);
	} else {
	    $result_cb->(undef, $res, 'FAKELABEL', $ACCESS_WRITE);
	}
    });

    # delay this load call a little bit -- just enough so that the
    # request_volume_permission event reliably occurs first
    Amanda::MainLoop::call_after(50, sub {
	$self->{'chg'}->load(@slotarg, set_current => 1, res_cb => $res_cb);
    });
}

package Mock::Feedback;
use base qw( Amanda::Taper::Scribe::Feedback );
use Test::More;
use Data::Dumper;
use Installcheck::Config;

sub new {
    my $class = shift;
    my @rq_answers = @_;
    return bless {
	rq_answers => [ @rq_answers ],
    }, $class;
}

sub request_volume_permission {
    my $self = shift;
    my %params = @_;
    my $answer = shift @{$self->{'rq_answers'}} || [undef, undef];
    main::event("request_volume_permission", "answer:", $answer);
    $params{'perm_cb'}->(@$answer);
}

sub notif_new_tape {
    my $self = shift;
    my %params = @_;

    main::event("notif_new_tape",
	main::undef_or_str($params{'error'}), $params{'volume_label'});
}

sub notif_part_done {
    my $self = shift;
    my %params = @_;

    # this omits $duration, as it's not constant
    main::event("notif_part_done",
	$params{'partnum'}, $params{'fileno'},
	$params{'successful'}, $params{'size'});
}


##
## test DevHandling
##

package main;

# utility fn to stringify changer errors (earlier perls' Test::More's
# fail to do this automatically)
sub undef_or_str { (defined $_[0])? "".$_[0] : undef; }

sub run_devh {
    my ($nruns, $taperscan, $feedback) = @_;
    my $devh;
    reset_events();

    reset_taperoot($nruns);
    $devh = Amanda::Taper::Scribe::DevHandling->new(
	taperscan => $taperscan,
	feedback => $feedback);

    my ($start, $get_volume, $got_volume, $quit);

    $start = make_cb(start => sub {
	event("start");
	$devh->start();

	# give start() time to get the scan going before
	# calling get_volume -- this wouldn't ordinarily be
	# necessary, but we want to make sure that start() is
	# really kicking off the scan.
	$get_volume->();
    });

    my $runcount = 0;
    $get_volume = make_cb(get_volume => sub {
	if (++$runcount > $nruns) {
	    $quit->();
	    return
	}

	event("get_volume");
	$devh->get_volume(volume_cb => $got_volume);
    });

    $got_volume = make_cb(got_volume => sub {
	my ($scan_error, $config_denial_message, $error_denial_message,
	    $reservation, $volume_label, $access_mode) = @_;

	event("got_volume",
	    undef_or_str($scan_error),
	    $config_denial_message, $error_denial_message,
	    $reservation? ("slot: ".$reservation->{'this_slot'}) : undef);

	if ($scan_error or $config_denial_message or $error_denial_message) {
	    $quit->();
	    return;
	}

	$reservation->release(finished_cb => sub {
	    my ($error) = @_;
	    event("release", $error);
	    if ($error) {
		$quit->();
	    } else {
		$get_volume->();
	    }
	});
    });

    $quit = make_cb(quit => sub {
	event("quit");
	Amanda::MainLoop::quit();
    });

    $start->();
    Amanda::MainLoop::run();
}

reset_taperoot(1);
run_devh(3, Mock::Taperscan->new(), Mock::Feedback->new());
is_deeply([ @events ], [
      [ 'start' ],
      [ 'scan' ], # scan starts *before* get_volume

      [ 'get_volume' ],
      [ 'request_volume_permission', 'answer:', [ undef, undef ] ],
      [ 'scan-finished', undef, "slot: 1" ],
      [ 'got_volume', undef, undef, undef, "slot: 1" ],
      [ 'release', undef ],

      [ 'get_volume' ],
      [ 'scan' ], # scan starts *after* get_volume this time
      [ 'request_volume_permission', 'answer:', [ undef, undef ] ],
      [ 'scan-finished', undef, "slot: 2" ],
      [ 'got_volume', undef, undef, undef, "slot: 2" ],
      [ 'release', undef ],

      [ 'get_volume' ],
      [ 'scan' ],
      [ 'request_volume_permission', 'answer:', [ undef, undef ] ],
      [ 'scan-finished', undef, "slot: 3" ],
      [ 'got_volume', undef, undef, undef, "slot: 3" ],
      [ 'release', undef ],

      [ 'quit' ],
    ], "correct event sequence for basic run of DevHandling")
    or diag(Dumper([@events]));

run_devh(1, Mock::Taperscan->new(), Mock::Feedback->new(['config', 'no-can-do']));
is_deeply([ @events ], [
      [ 'start' ],
      [ 'scan' ],

      [ 'get_volume' ],
      [ 'request_volume_permission', 'answer:', ['config','no-can-do'] ],
      [ 'scan-finished', undef, "slot: 1" ],
      [ 'got_volume', undef, 'no-can-do', undef, undef ],

      [ 'quit' ],
    ], "correct event sequence for a run without permission")
    or diag(Dumper([@events]));

run_devh(1, Mock::Taperscan->new("bogus"), Mock::Feedback->new());
is_deeply([ @events ], [
      [ 'start' ],
      [ 'scan' ],

      [ 'get_volume' ],
      [ 'request_volume_permission', 'answer:', [ undef, undef ] ],
      [ 'scan-finished', "Slot bogus not found", "slot: none" ],
      [ 'got_volume', 'Slot bogus not found', undef, undef, undef ],

      [ 'quit' ],
    ], "correct event sequence for a run with a changer error")
    or diag(Dumper([@events]));

run_devh(1, Mock::Taperscan->new("bogus"), Mock::Feedback->new(['config',"not this time"]));
is_deeply([ @events ], [
      [ 'start' ],
      [ 'scan' ],

      [ 'get_volume' ],
      [ 'request_volume_permission', 'answer:', ['config','not this time'] ],
      [ 'scan-finished', "Slot bogus not found", "slot: none" ],
      [ 'got_volume', 'Slot bogus not found', 'not this time', undef, undef ],

      [ 'quit' ],
    ], "correct event sequence for a run with no permission AND a changer config denial")
    or diag(Dumper([@events]));

run_devh(1, Mock::Taperscan->new("bogus"), Mock::Feedback->new(['error',"frobnicator exploded!"]));
is_deeply([ @events ], [
      [ 'start' ],
      [ 'scan' ],

      [ 'get_volume' ],
      [ 'request_volume_permission', 'answer:', ['error',"frobnicator exploded!"] ],
      [ 'scan-finished', "Slot bogus not found", "slot: none" ],
      [ 'got_volume', 'Slot bogus not found', undef, "frobnicator exploded!", undef ],

      [ 'quit' ],
    ], "correct event sequence for a run with no permission AND a changer error")
    or diag(Dumper([@events]));

##
## test Scribe
##

sub run_scribe_xfer_async {
    my ($data_length, $scribe, %params) = @_;
    my $xfer;

    my $finished_cb = $params{'finished_cb'};
    my $steps = define_steps
	cb_ref => \$finished_cb;

    step start_scribe => sub {
	if ($params{'start_scribe'}) {
	    $scribe->start(%{ $params{'start_scribe'} },
			finished_cb => $steps->{'get_xdt'});
	} else {
	    $steps->{'get_xdt'}->();
	}
    };

    step get_xdt => sub {
	my ($err) = @_;
	die $err if $err;

	# set up a transfer
	my $xdt = $scribe->get_xfer_dest(
	    max_memory => 1024 * 64,
            split_method => ($params{'split_method'} or 'memory'),
	    part_size => 1024 * 128,
	    use_mem_cache => 1,
	    disk_cache_dirname => undef);

        die "$err" if $err;

	my $hdr = Amanda::Header->new();
	$hdr->{type} = $Amanda::Header::F_DUMPFILE;
	$hdr->{datestamp} = "20010203040506";
	$hdr->{dumplevel} = 0;
	$hdr->{compressed} = 1;
	$hdr->{name} = "localhost";
	$hdr->{disk} = "/home";
	$hdr->{program} = "INSTALLCHECK";

        $xfer = Amanda::Xfer->new([
            Amanda::Xfer::Source::Random->new($data_length, 0x5EED5),
            $xdt,
        ]);

        $xfer->start(sub {
            $scribe->handle_xmsg(@_);
        });

        $scribe->start_dump(
	    xfer => $xfer,
            dump_header => $hdr,
            dump_cb => $steps->{'dump_cb'});
    };

    step dump_cb => sub {
	my %params = @_;

	main::event("dump_cb",
	    $params{'result'},
	    [ map { "$_" } @{$params{'device_errors'}} ],
	    $params{'config_denial_message'},
	    $params{'size'});

	$finished_cb->();
    };
}

sub run_scribe_xfer {
    my ($data_length, $scribe, %params) = @_;
    $params{'finished_cb'} = \&Amanda::MainLoop::quit;
    run_scribe_xfer_async($data_length, $scribe, %params);
    Amanda::MainLoop::run();
}

sub quit_scribe {
    my ($scribe) = @_;

    my $finished_cb = make_cb(finished_cb => sub {
	my ($error) = @_;
	die "$error" if $error;

	Amanda::MainLoop::quit();
    });

    $scribe->quit(finished_cb => $finished_cb);

    Amanda::MainLoop::run();
}

my $scribe;
my $experr;

# write less than a tape full

reset_taperoot(1);
$scribe = Amanda::Taper::Scribe->new(
    taperscan => Mock::Taperscan->new(),
    feedback => Mock::Feedback->new());

reset_events();
run_scribe_xfer(1024*300, $scribe,
	    start_scribe => { write_timestamp => "20010203040506" });

is_deeply([ @events ], [
      [ 'scan' ],
      [ 'scan-finished', undef, 'slot: 1' ],
      [ 'request_volume_permission', 'answer:', [ undef, undef ] ],
      [ 'notif_new_tape', undef, 'FAKELABEL' ],
      [ 'notif_part_done', bi(1), bi(1), 1, bi(131072) ],
      [ 'notif_part_done', bi(2), bi(2), 1, bi(131072) ],
      [ 'notif_part_done', bi(3), bi(3), 1, bi(45056) ],
      [ 'dump_cb', 'DONE', [], undef, bi(307200) ],
    ], "correct event sequence for a multipart scribe of less than a whole volume")
    or diag(Dumper([@events]));

# pick up where we left off, writing just a tiny bit more
reset_events();
run_scribe_xfer(1024*30, $scribe);

is_deeply([ @events ], [
      [ 'notif_part_done', bi(1), bi(4), 1, bi(30720) ],
      [ 'dump_cb', 'DONE', [], undef, bi(30720) ],
    ], "correct event sequence for a subsequent single-part scribe, still on the same volume")
    or diag(Dumper([@events]));

quit_scribe($scribe);

# start over again and try a multivolume write
#
# NOTE: the part size and volume size are such that the VFS driver produces
# ENOSPC while writing the fourth file header, rather than while writing
# data.  This is a much less common error path, so it's good to test it.

reset_taperoot(2);
$scribe = Amanda::Taper::Scribe->new(
    taperscan => Mock::Taperscan->new(),
    feedback => Mock::Feedback->new());

reset_events();
run_scribe_xfer($volume_length + $volume_length / 4, $scribe,
	    start_scribe => { write_timestamp => "20010203040506" });

is_deeply([ @events ], [
      [ 'scan' ],
      [ 'scan-finished', undef, 'slot: 1' ],
      [ 'request_volume_permission', 'answer:', [ undef, undef ] ],
      [ 'notif_new_tape', undef, 'FAKELABEL' ],

      [ 'notif_part_done', bi(1), bi(1), 1, bi(131072) ],
      [ 'notif_part_done', bi(2), bi(2), 1, bi(131072) ],
      [ 'notif_part_done', bi(3), bi(3), 1, bi(131072) ],
      [ 'notif_part_done', bi(4), bi(0), 0, bi(0) ],

      [ 'scan' ],
      [ 'request_volume_permission', 'answer:', [ undef, undef ] ],
      [ 'scan-finished', undef, 'slot: 2' ],
      [ 'notif_new_tape', undef, 'FAKELABEL' ],

      [ 'notif_part_done', bi(4), bi(1), 1, bi(131072) ],
      [ 'notif_part_done', bi(5), bi(2), 1, bi(131072) ],
      # empty part is written but not notified

      [ 'dump_cb', 'DONE', [], undef, bi(655360) ],
    ], "correct event sequence for a multipart scribe of more than a whole volume")
    or print (Dumper([@events]));

quit_scribe($scribe);

# now a multivolume write where the second volume gives a changer error

reset_taperoot(1);
$scribe = Amanda::Taper::Scribe->new(
    taperscan => Mock::Taperscan->new("1", "bogus"),
    feedback => Mock::Feedback->new());

reset_events();
run_scribe_xfer($volume_length + $volume_length / 4, $scribe,
	    start_scribe => { write_timestamp => "20010203040507" });

$experr = 'Slot bogus not found';
is_deeply([ @events ], [
      [ 'scan' ],
      [ 'scan-finished', undef, 'slot: 1' ],
      [ 'request_volume_permission', 'answer:', [ undef, undef ] ],
      [ 'notif_new_tape', undef, 'FAKELABEL' ],

      [ 'notif_part_done', bi(1), bi(1), 1, bi(131072) ],
      [ 'notif_part_done', bi(2), bi(2), 1, bi(131072) ],
      [ 'notif_part_done', bi(3), bi(3), 1, bi(131072) ],
      [ 'notif_part_done', bi(4), bi(0), 0, bi(0) ],

      [ 'scan' ],
      [ 'request_volume_permission', 'answer:', [ undef, undef ] ],
      [ 'scan-finished', $experr, 'slot: none' ],
      [ 'notif_new_tape', $experr, undef ],

      [ 'dump_cb', 'PARTIAL', [$experr], undef, bi(393216) ],
    ], "correct event sequence for a multivolume scribe where the second volume isn't found")
    or print (Dumper([@events]));

quit_scribe($scribe);

# now a multivolume write where the second volume does not have permission

reset_taperoot(2);
$scribe = Amanda::Taper::Scribe->new(
    taperscan => Mock::Taperscan->new(),
    feedback => Mock::Feedback->new(undef, ['config',"sorry!"]));

reset_events();
run_scribe_xfer($volume_length + $volume_length / 4, $scribe,
	    start_scribe => { write_timestamp => "20010203040507" });

is_deeply([ @events ], [
      [ 'scan' ],
      [ 'scan-finished', undef, 'slot: 1' ],
      [ 'request_volume_permission', 'answer:', [ undef, undef ] ],
      [ 'notif_new_tape', undef, 'FAKELABEL' ],

      [ 'notif_part_done', bi(1), bi(1), 1, bi(131072) ],
      [ 'notif_part_done', bi(2), bi(2), 1, bi(131072) ],
      [ 'notif_part_done', bi(3), bi(3), 1, bi(131072) ],
      [ 'notif_part_done', bi(4), bi(0), 0, bi(0) ],

      [ 'scan' ],
      [ 'request_volume_permission', 'answer:', ['config',"sorry!"] ],
      [ 'scan-finished', undef, 'slot: 2' ],

      [ 'dump_cb', 'PARTIAL', [], "sorry!", bi(393216) ],
    ], "correct event sequence for a multivolume scribe where the second volume isn't permitted")
    or print (Dumper([@events]));

quit_scribe($scribe);

# a non-splitting xfer on a single volume

reset_taperoot(2);
$scribe = Amanda::Taper::Scribe->new(
    taperscan => Mock::Taperscan->new(),
    feedback => Mock::Feedback->new());

reset_events();
run_scribe_xfer(1024*300, $scribe, split_method => 'none',
	    start_scribe => { write_timestamp => "20010203040506" });

is_deeply([ @events ], [
      [ 'scan' ],
      [ 'scan-finished', undef, 'slot: 1' ],
      [ 'request_volume_permission', 'answer:', [ undef, undef ] ],
      [ 'notif_new_tape', undef, 'FAKELABEL' ],
      [ 'notif_part_done', bi(1), bi(1), 1, bi(307200) ],
      [ 'dump_cb', 'DONE', [], undef, bi(307200) ],
    ], "correct event sequence for a non-splitting scribe of less than a whole volume")
    or diag(Dumper([@events]));

quit_scribe($scribe);

# DirectTCP support is tested through the taper installcheck

# test get_splitting_args_from_config thoroughly
my $maxint64 = Math::BigInt->new("9223372036854775808");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 0,
    ) },
    { split_method => 'none' },
    "default for no cache_inform is split_method none");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 1,
    ) },
    { split_method => 'none' },
    "default for cache_inform is also split_method none");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 1,
	tape_splitsize => 0,
	split_diskbuffer => $Installcheck::TMP,
	fallback_splitsize => 100,
    ) },
    { split_method => 'none' },
    "tape_splitsize = 0 indicates split_method none, not fallback");

is_deeply(
    { get_splitting_args_from_config(
	dle_allow_split => 0,
	can_cache_inform => 1,
	part_size => 100,
	part_cache_dir => "/tmp",
    ) },
    { split_method => 'none' },
    "default if dle_allow_split is false, no splitting");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 1,
	dle_tape_splitsize => 200,
	dle_fallback_splitsize => 250,
    ) },
    { split_method => 'cache_inform', part_size => 200 },
    "when cache_inform is available, tape_splitsize is used, not fallback");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 0,
	dle_tape_splitsize => 200,
	dle_fallback_splitsize => 250,
    ) },
    { split_method => 'memory', part_size => 250, },
    "if split_diskbuffer is missing, fall back");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 0,
	dle_tape_splitsize => 200,
    ) },
    { split_method => 'memory', part_size => 1024*1024*10, },
    "no split_diskbuffer and no fallback_splitsize, fall back to default (10M)");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 0,
	dle_tape_splitsize => 200,
	dle_split_diskbuffer => "$Installcheck::TMP/does!not!exist!",
	dle_fallback_splitsize => 250,
    ) },
    { split_method => 'memory', part_size => 250, },
    "invalid split_diskbuffer => fall back (silently)");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 0,
	dle_tape_splitsize => 200,
	dle_split_diskbuffer => "$Installcheck::TMP/does!not!exist!",
    ) },
    { split_method => 'memory', part_size => 1024*1024*10, },
    ".. even to the default fallback (10M)");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 0,
	dle_tape_splitsize => $maxint64,
	dle_split_diskbuffer => "$Installcheck::TMP",
	dle_fallback_splitsize => 250,
    ) },
    { split_method => 'memory', part_size => 250,
      warning => "falling back to memory buffer for splitting: " .
		 "insufficient space in disk cache directory" },
    "not enough space in split_diskbuffer => fall back (with warning)");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 0,
	dle_tape_splitsize => 200,
	dle_split_diskbuffer => "$Installcheck::TMP",
	dle_fallback_splitsize => 250,
    ) },
    { split_method => 'disk', part_size => 200,
      disk_cache_dirname => "$Installcheck::TMP" },
    "if split_diskbuffer exists and splitsize is nonzero, use it");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 0,
	dle_tape_splitsize => 0,
	dle_split_diskbuffer => "$Installcheck::TMP",
	dle_fallback_splitsize => 250,
    ) },
    { split_method => 'none' },
    ".. but if splitsize is zero, no splitting");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 0,
	dle_split_diskbuffer => "$Installcheck::TMP",
	dle_fallback_splitsize => 250,
    ) },
    { split_method => 'none' },
    ".. and if splitsize is missing, no splitting");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 1,
	part_size => 300,
    ) },
    { split_method => 'cache_inform', part_size => 300 },
    "With cache_inform and a part_size, splitting is done");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 0,
	part_size => 300,
	part_cache_type => 'none',
    ) },
    { split_method => 'none' },
    "part_cache_type 'none' translates correctly");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 0,
	part_size => 300,
	part_cache_type => 'memory',
    ) },
    { split_method => 'memory', part_size => 300 },
    "part_cache_type 'memory' translates correctly");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 0,
	part_size => 300,
	part_cache_type => 'memory',
	part_cache_max_size => 100,
    ) },
    { split_method => 'memory', part_size => 100 },
    ".. and part_cache_max_size is minded");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 0,
	part_size => 250,
	part_cache_type => 'memory',
	part_cache_max_size => 500,
    ) },
    { split_method => 'memory', part_size => 250 },
    ".. but treated as a maximum");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 0,
	part_size => 300,
	part_cache_type => 'disk',
	part_cache_dir => $Installcheck::TMP,
    ) },
    { split_method => 'disk',
      part_size => 300,
      disk_cache_dirname => $Installcheck::TMP },
    "part_cache_type 'disk' translates correctly");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 0,
	part_size => 300,
	part_cache_type => 'disk',
	part_cache_dir => $Installcheck::TMP,
	part_cache_max_size => 100,
    ) },
    { split_method => 'disk',
      part_size => 100,
      disk_cache_dirname => $Installcheck::TMP },
    ".. and part_cache_max_size is minded");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 0,
	part_size => 300,
	part_cache_type => 'disk',
	part_cache_dir => "$Installcheck::TMP/does!not!exist!",
    ) },
    { split_method => 'none',
      warning => "not caching split parts: " .
		 "insufficient space for split cache in part_cache_dir" },
    "warning when part_size is larger than part_cache_dir");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 0,
	part_size => 300,
	part_cache_type => 'disk',
    ) },
    { split_method => 'none',
      warning => "not caching split parts: " .
		 "insufficient space for split cache in part_cache_dir" },
    "warning when part_size is larger than part_cache_dir");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 0,
	part_size => $maxint64,
	part_cache_type => 'disk',
	part_cache_dir => $Installcheck::TMP
    ) },
    { split_method => 'none',
      warning => "not caching split parts: " .
		 "insufficient space for split cache in part_cache_dir" },
    "warning when part_size is larger than part_cache_dir");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 1,
	part_size => 0,
    ) },
    { split_method => 'none' },
    "a zero partsize with can_cache_inform is passed along properly");

is_deeply(
    { get_splitting_args_from_config(
	can_cache_inform => 0,
	part_size => 0,
    ) },
    { split_method => 'none' },
    "a zero partsize without can_cache_inform is passed along properly");

rmtree($taperoot);
