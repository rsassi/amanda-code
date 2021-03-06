# Copyright (c) 2008,2009,2010 Zmanda, Inc.  All Rights Reserved.
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
# Contact information: Zmanda Inc., 465 S. Mathilda Ave., Suite 300
# Sunnyvale, CA 94085, USA, or: http://www.zmanda.com

package Amanda::Changer::disk;

use strict;
use warnings;
use vars qw( @ISA );
@ISA = qw( Amanda::Changer );

use File::Glob qw( :glob );
use File::Path;
use Amanda::Config qw( :getconf );
use Amanda::Debug;
use Amanda::Changer;
use Amanda::MainLoop;
use Amanda::Device qw( :constants );

=head1 NAME

Amanda::Changer::disk

=head1 DESCRIPTION

This changer operates within a root directory, specified in the changer
string, which it arranges as follows:

  $dir -|
        |- drive0/ -|
        |           | data -> '../slot4'
        |- drive1/ -|
        |           | data -> '../slot1'
        |- data -> slot5
        |- slot1/
        |- slot2/
        |- ...
        |- slot$n/

The user should create the desired number of C<slot$n> subdirectories.  The
changer will take care of dynamically creating the drives as needed, and track
the current slot using a "data" symlink.  This allows use of "file:$dir" as a
device operating on the current slot, although note that it is unlocked.

Drives are dynamically allocated as Amanda applications request access to
particular slots.  Each drive is represented as a subdirectory containing a
'data' symlink pointing to the "loaded" slot.

See the amanda-changers(7) manpage for usage information.

=cut

# STATE
#
# The device state is shared between all changers accessing the same changer.
# It is a hash with keys:
#   drives - see below
#
# The 'drives' key is a hash, with drive as keys and hashes
# as values.  Each drive's hash has keys:
#   pid - the pid that reserved that drive.
#


sub new {
    my $class = shift;
    my ($config, $tpchanger) = @_;
    my ($dir) = ($tpchanger =~ /chg-disk:(.*)/);

    unless (-d $dir) {
	return Amanda::Changer->make_error("fatal", undef,
	    message => "directory '$dir' does not exist");
    }

    # note that we don't track outstanding Reservation objects -- we know
    # they're gone when they delete their drive directory
    my $self = {
	dir => $dir,
	config => $config,
	state_filename => "$dir/state",

	# this is set to 0 by various test scripts,
	# notably Amanda_Taper_Scan_traditional
	support_fast_search => 1,
    };

    bless ($self, $class);
    return $self;
}

sub load {
    my $self = shift;
    my %params = @_;
    my $old_res_cb = $params{'res_cb'};
    my $state;

    $self->validate_params('load', \%params);

    return if $self->check_error($params{'res_cb'});

    $self->with_locked_state($self->{'state_filename'},
				     $params{'res_cb'}, sub {
	my ($state, $res_cb) = @_;
	$params{'state'} = $state;

	# overwrite the callback for _load_by_xxx
	$params{'res_cb'} = $res_cb;

	if (exists $params{'slot'} or exists $params{'relative_slot'}) {
	    $self->_load_by_slot(%params);
	} elsif (exists $params{'label'}) {
	    $self->_load_by_label(%params);
	}
    });
}

sub info_key {
    my $self = shift;
    my ($key, %params) = @_;
    my %results;

    return if $self->check_error($params{'info_cb'});

    # no need for synchronization -- all of these values are static

    if ($key eq 'num_slots') {
	my @slots = $self->_all_slots();
	$results{$key} = scalar @slots;
    } elsif ($key eq 'vendor_string') {
	$results{$key} = 'chg-disk'; # mostly just for testing
    } elsif ($key eq 'fast_search') {
	$results{$key} = $self->{'support_fast_search'};
    }

    $params{'info_cb'}->(undef, %results) if $params{'info_cb'};
}

sub reset {
    my $self = shift;
    my %params = @_;
    my $slot;
    my @slots = $self->_all_slots();

    return if $self->check_error($params{'finished_cb'});

    $self->with_locked_state($self->{'state_filename'},
				     $params{'finished_cb'}, sub {
	my ($state, $finished_cb) = @_;

	$slot = (scalar @slots)? $slots[0] : 0;
	$self->_set_current($slot);

	$finished_cb->();
    });
}

sub inventory {
    my $self = shift;
    my %params = @_;

    return if $self->check_error($params{'inventory_cb'});

    my @slots = $self->_all_slots();

    $self->with_locked_state($self->{'state_filename'},
			     $params{'inventory_cb'}, sub {
	my ($state, $finished_cb) = @_;
	my @inventory;

	my $current = $self->_get_current();
	for my $slot (@slots) {
	    my $s = { slot => $slot, state => Amanda::Changer::SLOT_FULL };
	    $s->{'reserved'} = $self->_is_slot_in_use($state, $slot);
	    my $label = $self->_get_slot_label($slot);
	    if ($label) {
		$s->{'label'} = $self->_get_slot_label($slot);
		$s->{'f_type'} = "".$Amanda::Header::F_TAPESTART;
	    } else {
		$s->{'label'} = undef;
		$s->{'f_type'} = "".$Amanda::Header::F_EMPTY;
	    }
	    $s->{'device_status'} = "".$DEVICE_STATUS_SUCCESS;
	    $s->{'current'} = 1 if $slot eq $current;
	    push @inventory, $s;
	}
	$finished_cb->(undef, \@inventory);
    });
}

sub _load_by_slot {
    my $self = shift;
    my %params = @_;
    my $drive;
    my $slot;

    if (exists $params{'relative_slot'}) {
	if ($params{'relative_slot'} eq "current") {
	    $slot = $self->_get_current();
	} elsif ($params{'relative_slot'} eq "next") {
	    if (exists $params{'slot'}) {
		$slot = $params{'slot'};
	    } else {
		$slot = $self->_get_current();
	    }
	    $slot = $self->_get_next($slot);
	    $self->_set_current($slot) if ($params{'set_current'});
	} else {
	    return $self->make_error("failed", $params{'res_cb'},
		reason => "invalid",
		message => "Invalid relative slot '$params{relative_slot}'");
	}
    } else {
	$slot = $params{'slot'};
    }

    if (exists $params{'except_slots'} and exists $params{'except_slots'}->{$slot}) {
	return $self->make_error("failed", $params{'res_cb'},
	    reason => "notfound",
	    message => "all slots have been loaded");
    }

    if (!$self->_slot_exists($slot)) {
	return $self->make_error("failed", $params{'res_cb'},
	    reason => "invalid",
	    message => "Slot $slot not found");
    }

    if ($drive = $self->_is_slot_in_use($params{'state'}, $slot)) {
	return $self->make_error("failed", $params{'res_cb'},
	    reason => "volinuse",
	    slot => $slot,
	    message => "Slot $slot is already in use by drive '$drive' and process '$params{state}->{drives}->{$drive}->{pid}'");
    }

    $drive = $self->_alloc_drive();
    $self->_load_drive($drive, $slot);
    $self->_set_current($slot) if ($params{'set_current'});

    $self->_make_res($params{'state'}, $params{'res_cb'}, $drive, $slot);
}

sub _load_by_label {
    my $self = shift;
    my %params = @_;
    my $label = $params{'label'};
    my $slot;
    my $drive;

    $slot = $self->_find_label($label);
    if (!defined $slot) {
	return $self->make_error("failed", $params{'res_cb'},
	    reason => "notfound",
	    message => "Label '$label' not found");
    }

    if ($drive = $self->_is_slot_in_use($params{'state'}, $slot)) {
	return $self->make_error("failed", $params{'res_cb'},
	    reason => "volinuse",
	    message => "Slot $slot, containing '$label', is already " .
			"in use by drive '$drive'");
    }

    $drive = $self->_alloc_drive();
    $self->_load_drive($drive, $slot);
    $self->_set_current($slot) if ($params{'set_current'});

    $self->_make_res($params{'state'}, $params{'res_cb'}, $drive, $slot);
}

sub _make_res {
    my $self = shift;
    my ($state, $res_cb, $drive, $slot) = @_;
    my $res;

    my $device = Amanda::Device->new("file:$drive");
    if ($device->status != $DEVICE_STATUS_SUCCESS) {
	return $self->make_error("failed", $res_cb,
		reason => "device",
		message => "opening 'file:$drive': " . $device->error_or_status());
    }

    if (my $err = $self->{'config'}->configure_device($device)) {
	return $self->make_error("failed", $res_cb,
		reason => "device",
		message => $err);
    }

    $res = Amanda::Changer::disk::Reservation->new($self, $device, $drive, $slot);
    $state->{drives}->{$drive}->{pid} = $$;
    $device->read_label();

    $res_cb->(undef, $res);
}

# Internal function to find an unused (nonexistent) driveN subdirectory and
# create it.  Note that this does not add a 'data' symlink inside the directory.
sub _alloc_drive {
    my ($self) = @_;
    my $n = 0;

    while (1) {
	my $drive = $self->{'dir'} . "/drive$n";
	$n++;

	warn "$drive is not a directory; please remove it" if (-e $drive and ! -d $drive);
	next if (-e $drive);
	next if (!mkdir($drive)); # TODO probably not a very effective locking mechanism..

	return $drive;
    }
}

# Internal function to enumerate all available slots.  Slots are described by
# integers.
sub _all_slots {
    my ($self) = @_;
    my $dir = _quote_glob($self->{'dir'});
    my @slots;

    for my $slotname (bsd_glob("$dir/slot*/")) {
	my $slot;
	next unless (($slot) = ($slotname =~ /.*slot([0-9]+)\/$/));
	push @slots, $slot + 0;
    }

    return sort @slots;
}

# Internal function to determine whether a slot exists.
sub _slot_exists {
    my ($self, $slot) = @_;
    return (-d $self->{'dir'} . "/slot$slot");
}

# Internal function to determine if a slot (specified by number) is in use by a
# drive, and return the path for that drive if so.
sub _is_slot_in_use {
    my ($self, $state, $slot) = @_;
    my $dir = _quote_glob($self->{'dir'});

    for my $symlink (bsd_glob("$dir/drive*/data")) {
	if (! -l $symlink) {
	    warn "'$symlink' is not a symlink; please remove it";
	    next;
	}

	my $target = readlink($symlink);
	if (!$target) {
	    warn "could not read '$symlink': $!";
	    next;
	}

	my $tslot;
	if (!(($tslot) = ($target =~ /..\/slot([0-9]+)/))) {
	    warn "invalid changer symlink '$symlink' -> '$target'";
	    next;
	}

	if ($tslot+0 == $slot) {
	    my $drive = $symlink;
	    $drive =~ s{/data$}{}; # strip the trailing '/data'

	    #check if process is alive
	    my $pid = $state->{drives}->{$drive}->{pid};
	    if (!defined $pid or !Amanda::Util::is_pid_alive($pid)) {
		unlink("$drive/data")
		    or warn("Could not unlink '$drive/data': $!");
		rmdir("$drive")
		    or warn("Could not rmdir '$drive': $!");
		delete $state->{drives}->{$drive}->{pid};
		next;
	    }
	    return $drive;
	}
    }

    return 0;
}

sub _get_slot_label {
    my ($self, $slot) = @_;
    my $dir = _quote_glob($self->{'dir'});

    for my $symlink (bsd_glob("$dir/slot$slot/00000.*")) {
	my ($label) = ($symlink =~ qr{\/00000\.([^/]*)$});
	return $label;
    }

    return ''; # known, but blank
}

# Internal function to point a drive to a slot
sub _load_drive {
    my ($self, $drive, $slot) = @_;

    die "'$drive' does not exist" unless (-d $drive);
    if (-e "$drive/data") {
	unlink("$drive/data");
    }

    symlink("../slot$slot", "$drive/data");
    # TODO: read it to be sure??
}

# Internal function to return the slot containing a volume with the given
# label.  This takes advantage of the naming convention used by vtapes.
sub _find_label {
    my ($self, $label) = @_;
    my $dir = _quote_glob($self->{'dir'});
    $label = _quote_glob($label);

    my @tapelabels = bsd_glob("$dir/slot*/00000.$label");
    if (!@tapelabels) {
        return undef;
    }

    if (scalar @tapelabels > 1) {
        warn "Multiple slots with label '$label': " . (join ", ", @tapelabels);
    }

    my ($slot) = ($tapelabels[0] =~ qr{/slot([0-9]+)/00000.});
    return $slot;
}

# Internal function to get the next slot after $slot.
sub _get_next {
    my ($self, $slot) = @_;
    my $next_slot;

    # Try just incrementing the slot number
    $next_slot = $slot+1;
    return $next_slot if (-d $self->{'dir'} . "/slot$next_slot");

    # Otherwise, search through all slots
    my @all_slots = $self->_all_slots();
    my $prev = $all_slots[-1];
    for $next_slot (@all_slots) {
        return $next_slot if ($prev == $slot);
        $prev = $next_slot;
    }

    # not found? take a guess.
    return $all_slots[0];
}

# Get the 'current' slot, represented as a symlink named 'data'
sub _get_current {
    my ($self) = @_;
    my $curlink = $self->{'dir'} . "/data";

    # for 2.6.1-compatibility, also parse a "current" symlink
    my $oldlink = $self->{'dir'} . "/current";
    if (-l $oldlink and ! -e $curlink) {
	rename($oldlink, $curlink);
    }

    if (-l $curlink) {
        my $target = readlink($curlink);
        if ($target =~ "^slot([0-9]+)/?") {
            return $1;
        }
    }

    # get the first slot as a default
    my @slots = $self->_all_slots();
    return 0 unless (@slots);
    return $slots[0];
}

# Set the 'current' slot
sub _set_current {
    my ($self, $slot) = @_;
    my $curlink = $self->{'dir'} . "/data";

    if (-e $curlink) {
        unlink($curlink)
            or warn("Could not unlink '$curlink'");
    }

    # TODO: locking
    symlink("slot$slot", $curlink);
}

# utility function
sub _quote_glob {
    my ($filename) = @_;
    $filename =~ s/([]{}\\?*[])/\\$1/g;
    return $filename;
}

package Amanda::Changer::disk::Reservation;
use vars qw( @ISA );
@ISA = qw( Amanda::Changer::Reservation );

sub new {
    my $class = shift;
    my ($chg, $device, $drive, $slot) = @_;
    my $self = Amanda::Changer::Reservation::new($class);

    $self->{'chg'} = $chg;
    $self->{'drive'} = $drive;

    $self->{'device'} = $device;
    $self->{'this_slot'} = $slot;

    return $self;
}

sub do_release {
    my $self = shift;
    my %params = @_;
    my $drive = $self->{'drive'};

    unlink("$drive/data")
	or warn("Could not unlink '$drive/data': $!");
    rmdir("$drive")
	or warn("Could not rmdir '$drive': $!");

    # unref the device, for good measure
    $self->{'device'} = undef;

    if (exists $params{'unlocked'}) {
        my $state = $params{state};
	delete $state->{drives}->{$drive}->{pid};
        return $params{'finished_cb'}->();
    }

    $self->{chg}->with_locked_state($self->{chg}->{'state_filename'},
				    $params{'finished_cb'}, sub {
	my ($state, $finished_cb) = @_;

	delete $state->{drives}->{$drive}->{pid};

	$finished_cb->();
    });
}
