#! @PERL@
# Copyright (c) 2008 Zmanda Inc.  All Rights Reserved.
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
# Contact information: Zmanda Inc., 465 S Mathlida Ave, Suite 300
# Sunnyvale, CA 94086, USA, or: http://www.zmanda.com

# This is a tool to examine a device and generate a reasonable tapetype
# entry accordingly.

use lib '@amperldir@';
use strict;

use File::Basename;
use Getopt::Long;
use Math::BigInt;
use Amanda::BigIntCompat;

use Amanda::Device qw( :constants );
use Amanda::Debug qw( :logging );
use Amanda::Util qw( :constants );
use Amanda::Config qw( :init :getconf config_dir_relative );
use Amanda::MainLoop;
use Amanda::Xfer;
use Amanda::Constants;
use Amanda::Types;

# command-line options
my $opt_only_compression = 0;
my $opt_blocksize;
my $opt_tapetype_name = 'unknown-tapetype';
my $opt_force = 0;
my $opt_label = "amtapetype-".(int rand 2**31);
my $opt_device_name;

# global "hint" from the compression heuristic as to how fast this
# drive is.
my $device_speed_estimate;

# open up a device, optionally check its label, and start it in ACCESS_WRITE.
sub open_device {
    my $device = Amanda::Device->new($opt_device_name);
    if ($device->status() != $DEVICE_STATUS_SUCCESS) {
	die("Could not open device $opt_device_name: ".$device->error()."\n");
    }

    if (defined $opt_blocksize) {
	$device->property_set('BLOCK_SIZE', $opt_blocksize)
	    or die "Error setting blocksize: " . $device->error_or_status();
    }

    if (!$opt_force) {
	my $read_label_status = $device->read_label();
	if ($read_label_status & $DEVICE_STATUS_VOLUME_UNLABELED) {
	    if ($device->volume_label) {
		die "Volume in device $opt_device_name has Amanda label '" .
		    {$device->volume_label} . "'. Giving up.";
	    }
	} elsif ($read_label_status != $DEVICE_STATUS_SUCCESS) {
	    die "Error reading label: " . $device->error_or_status();
	}
    }

    return $device;
}

sub start_device {
    my ($device) = @_;

    if (!$device->start($ACCESS_WRITE, $opt_label, undef)) {
	die("Error writing label '$opt_label': ". $device->error_or_status());
    }

    return $device;
}

# Write a single file to the device, and record the results in STATS.
# write_one_file(
#   STATS => $stats_hashref,	(see below)
#   DEVICE => $dev,		(device to write to)
#   PATTERN => RANDOM or FIXED, (data pattern to write)
#   BYTES => nn,		(number of bytes; optional)
#   MAX_TIME => secs);		(cancel write after this time; optional)
#
# Returns 0 on success (including EOM), "TIMEOUT" on timeout, or an error message
# on failure.
#
# STATS is a multi-level hashref; write_one_file adds to any values
# already in the data structure.
#   $stats->{$pattern}->{TIME} - number of seconds spent writing
#   $stats->{$pattern}->{FILES} - number of files written
#   $stats->{$pattern}->{BYTES} - number of bytes written (approximate)
#
sub write_one_file(%) {
    my %options = @_;
    my $stats = $options{'STATS'} || { };
    my $device = $options{'DEVICE'};
    my $bytes = $options{'MAX_BYTES'} || 0;
    my $pattern = $options{'PATTERN'} || 'FIXED';
    my $max_time = $options{'MAX_TIME'} || 0;

    # start the device
    my $hdr = Amanda::Types::dumpfile_t->new();
    $hdr->{type} = $Amanda::Types::F_DUMPFILE;
    $hdr->{name} = "amtapetype";
    $hdr->{disk} = "/test";
    $hdr->{datestamp} = "X";
    $device->start_file($hdr)
	or return $device->error_or_status();

    # set up the transfer
    my ($source, $dest, $xfer);
    if ($pattern eq 'FIXED') {
	# a simple 256-byte pattern to dodge run length encoding.
	my $non_random_pattern = pack("C*", 0..255);
	$source = Amanda::Xfer::Source::Pattern->new($bytes, $non_random_pattern);
    } elsif ($pattern eq 'RANDOM') {
	$source = Amanda::Xfer::Source::Random->new($bytes, 1 + int rand 100);
    } else {
	die "Unknown PATTERN $pattern";
    }
    $dest = Amanda::Xfer::Dest::Device->new($device, 0);
    $xfer = Amanda::Xfer->new([$source, $dest]);

    # set up the relevant callbacks
    my ($timeout_src, $xfer_src, $spinner_src);
    my $got_error = 0;
    my $got_timeout = 0;

    $xfer_src = $xfer->get_source();
    $xfer_src->set_callback(sub {
	my ($src, $xmsg, $xfer) = @_;
	if ($xmsg->{type} == $Amanda::Xfer::XMSG_ERROR) {
	    $got_error = $xmsg->{message};
	}
	if ($xfer->get_status() == $Amanda::Xfer::XFER_DONE) {
	    Amanda::MainLoop::quit();
	}
    });

    if ($max_time) {
	$timeout_src = Amanda::MainLoop::timeout_source($max_time * 1000);
	$timeout_src->set_callback(sub {
	    my ($src) = @_;
	    $got_timeout = 1;
	    $xfer->cancel(); # will result in an XFER_DONE
	});
    }

    $spinner_src = Amanda::MainLoop::timeout_source(1000);
    $spinner_src->set_callback(sub {
	my ($src) = @_;
	my ($file, $block) = ($device->file(), $device->block());
	print STDERR "File $file, block $block    \r";
    });

    my $start_time = time();

    $xfer->start();
    Amanda::MainLoop::run();
    $xfer_src->remove();
    $spinner_src->remove();
    $timeout_src->remove() if ($timeout_src);
    print STDERR " " x 60, "\r";

    my $duration = time() - $start_time;

    # OK, we finished, update statistics (even if we saw an error)
    my $blocks_written = $device->block();
    my $block_size = $device->property_get("block_size");
    $stats->{$pattern}->{BYTES} += $blocks_written * $block_size;
    $stats->{$pattern}->{FILES} += 1;
    $stats->{$pattern}->{TIME}  += $duration;

    if ($device->status() != $Amanda::Device::DEVICE_STATUS_SUCCESS) {
	return $device->error_or_status();
    }

    if ($got_error) {
	return $got_error;
    }

    if ($got_timeout) {
	return "TIMEOUT";
    }

    return 0;
}

sub check_compression {
    my ($device) = @_;

    # Check compression status here by property query. If the device can answer
    # the question, there's no reason to investigate further.
    my $compression_enabled = $device->property_get("compression");

    if (defined $compression_enabled) {
	return $compression_enabled;
    }

    # Need to use heuristic to find out if compression is enabled.  Also, we
    # rewind between passes so that the second pass doesn't get some kind of
    # buffering advantage.

    print STDERR "Applying heuristic check for compression.\n";

    # We base our determination on whether it's faster to write random data or
    # patterned data.  That starts by writing random data for a short length of
    # time, then measuring the elapsed time and total data written.  Due to
    # potential delay in cancelling a transfer, the elapsed time will be a bit
    # longer than the intended time.   We then write the same amount of
    # patterned data, and again measure the elapsed time.  We can then
    # calculate the speeds of the two operations.  If the compressible speed
    # was faster by more than min_ratio, then we assume compression is enabled.

    my $compression_check_time = 60;
    my $compression_check_min_ratio = 1.2;

    my $stats = { };

    start_device($device);

    my $err = write_one_file(
		    DEVICE => $device,
		    STATS => $stats,
		    MAX_TIME => $compression_check_time,
		    PATTERN => 'RANDOM');

    if ($err != 'TIMEOUT') {
	die $err;
    }

    # restart the device to rewind it
    start_device($device);

    $err = write_one_file(
		    DEVICE => $device,
		    STATS => $stats,
		    MAX_BYTES => $stats->{'RANDOM'}->{'BYTES'},
		    PATTERN => 'FIXED');
    if ($err) {
	die $err;
    }

    # speed calculations are a little tricky: BigInt * float comes out to NaN, so we
    # cast the BigInts to float first
    my $random_speed = ($stats->{RANDOM}->{BYTES} . "") / $stats->{RANDOM}->{TIME};
    my $fixed_speed = ($stats->{FIXED}->{BYTES} . "") / $stats->{FIXED}->{TIME};

    print STDERR "Wrote random (uncompressible) data at $random_speed bytes/sec\n";
    print STDERR "Wrote fixed (compressible) data at $fixed_speed bytes/sec\n";

    # sock this away for make_tapetype's use
    $device_speed_estimate = $random_speed;

    $compression_enabled =
	($fixed_speed / $random_speed > $compression_check_min_ratio);
    return $compression_enabled;
}

sub make_tapetype {
    my ($device, $compression_enabled) = @_;
    my $blocksize = $device->property_get("BLOCK_SIZE");

    # First, write one very long file to get the total tape length
    print STDERR "Writing one file to fill the volume.\n";
    my $stats = {};
    start_device($device);
    my $err = write_one_file(
		DEVICE => $device,
		STATS => $stats,
		PATTERN => 'RANDOM');

    if ($stats->{RANDOM}->{BYTES} < 1024 * 1024 * 100) {
	die "Wrote less than 100MB to the device: $err\n";
    }
    my $volume_size_estimate = $stats->{RANDOM}->{BYTES};
    my $speed_estimate = (($stats->{RANDOM}->{BYTES}."") / 1024)
			/ $stats->{RANDOM}->{TIME};
    $speed_estimate = int $speed_estimate;
    print STDERR "Wrote $volume_size_estimate bytes at $speed_estimate kb/sec\n";

    # now we want to write about 100 filemarks; round down to the blocksize
    # to avoid counting padding as part of the filemark
    my $file_size = $volume_size_estimate / 100;
    $file_size -= $file_size % $blocksize;

    print STDERR "Writing smaller files ($file_size bytes) to determine filemark.\n";
    $stats = {};
    start_device($device);
    while (!write_one_file(
			DEVICE => $device,
			STATS => $stats,
			MAX_BYTES => $file_size,
			PATTERN => 'RANDOM')) { }

    my $filemark_estimate = ($volume_size_estimate - $stats->{RANDOM}->{BYTES})
			  / ($stats->{RANDOM}->{FILES} - 1);
    if ($filemark_estimate < 0) {
	$filemark_estimate = 0;
    }

    my $comment = "Created by amtapetype; compression "
	. ($compression_enabled? "enabled" : "disabled");

    # round these parameters to the nearest kb, since the parameters' units
    # are kb, not bytes
    my $volume_size_estimate_kb = $volume_size_estimate/1024;
    my $filemark_kb = $filemark_estimate/1024;

    # and suggest using device_property for blocksize if it's not an even multiple
    # of 1kb
    my $blocksize_line;
    if ($blocksize % 1024 == 0) {
	$blocksize_line = "blocksize " . $blocksize/1024 . " kbytes";
    } else {
	$blocksize_line = "# add device_property \"BLOCK_SIZE\" \"$blocksize\" to the device";
    }

    print <<EOF;
define tapetype $opt_tapetype_name {
    comment "$comment"
    length $volume_size_estimate_kb kbytes
    filemark $filemark_kb kbytes
    speed $speed_estimate kps
    $blocksize_line
}
EOF
}

sub usage {
    print STDERR <<EOF;
Usage: amtapetype [-h] [-c] [-f] [-b blocksize] [-t typename] [-l label]
		  [ [-o config_overwrite] ... ] device
        -h   Display this message
        -c   Only check hardware compression state
        -f   Run amtapetype even if the loaded volume is already in use
             or compression is enabled.
        -b   Blocksize to use (default 32k)
        -t   Name to give to the new tapetype definition
        -l   Label to write to the tape (default is randomly generated)
        -o   Overwrite configuration parameter (such as device properties)
    Blocksize can include an optional suffix (k, m, or g)
EOF
    exit(1);
}

## Application initialization

Amanda::Util::setup_application("amtapetype", "server", $CONTEXT_CMDLINE);
config_init(0, undef);

my $config_overwrites = new_config_overwrites($#ARGV+1);

Getopt::Long::Configure(qw(bundling));
GetOptions(
    'help|usage|?|h' => \&usage,
    'c' => \$opt_only_compression,
    'b=s' => sub {
	my ($num, $suff) = ($_[1] =~ /^([0-9]+)\s*(.*)$/);
	die "Invalid blocksize '$_[1]'" unless (defined $num);
	my $mult = (defined $suff)?
	    Amanda::Config::find_multiplier($suff) : 1;
	die "Invalid suffix '$suff'" unless ($mult);
	$opt_blocksize = $num * $mult;
    },
    't=s' => \$opt_tapetype_name,
    'f' => \$opt_force,
    'l' => \$opt_label,
    'o=s' => sub { add_config_overwrite_opt($config_overwrites, $_[1]); },
) or usage();
usage() if (@ARGV != 1);

$opt_device_name= shift @ARGV;

apply_config_overwrites($config_overwrites);
my ($cfgerr_level, @cfgerr_errors) = config_errors();
if ($cfgerr_level >= $CFGERR_WARNINGS) {
    config_print_errors();
    if ($cfgerr_level >= $CFGERR_ERRORS) {
	die("errors processing configuration options");
    }
}

Amanda::Util::finish_setup($RUNNING_AS_ANY);

my $device = open_device();

my $compression_enabled = check_compression($device);
print STDERR "Compression: ",
    $compression_enabled? "enabled" : "disabled",
    "\n";

if ($compression_enabled and !$opt_force) {
    print STDERR "Turn off compression or run amtapetype with the -f option\n";
    exit(1);
}

if (!$opt_only_compression) {
    make_tapetype($device, $compression_enabled);
}
