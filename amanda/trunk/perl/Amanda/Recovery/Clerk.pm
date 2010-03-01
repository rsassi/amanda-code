# Copyright (c) 2010 Zmanda, Inc.  All Rights Reserved.
#
# This library is free software; you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License version 2.1 as
# published by the Free Software Foundation.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
# License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this library; if not, write to the Free Software Foundation,
# Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
#
# Contact information: Zmanda Inc., 465 S. Mathilda Ave., Suite 300
# Sunnyvale, CA 94086, USA, or: http://www.zmanda.com

package Amanda::Recovery::Clerk;

use strict;
use warnings;
use Carp;

use Amanda::Xfer qw( :constants );
use Amanda::Device qw( :constants );
use Amanda::Header;
use Amanda::Holding;
use Amanda::Debug qw( :logging );
use Amanda::MainLoop;

=head1 NAME

Amanda::Recovery::Clerk - handle assembling dumpfiles from multiple parts

=head1 SYNOPSIS

    my $clerk = Amanda::Recovery::Clerk->new(
	scan => $scan)

    $subs{'setup'} = make_cb(setup => sub {
      $clerk->get_xfer_src(
	    dump => $dump, # from Amanda::Recovery::Planner or Amanda::DB::Catalog
	    xfer_src_cb => $subs{'xfer_src_cb'});
    });

    $subs{'xfer_src_cb'} = make_cb(xfer_src_cb => sub {
	my ($errors, $header, $xfer_src, $directtcp_supported) = @_;
	die join("\n", @$errors) if ($errors);
	print "restoring from " . $header->summary() . "\n";

	my $xfer = Amanda::Xfer->new([$xfer_src, $xfer_dest]);
	$xfer->start(sub { $clerk->handle_xmsg(@_); });
	$clerk->start_recovery(
	    xfer => $xfer,
	    recovery_cb => $subs{'recovery_cb'});
    });

    $subs{'recovery_cb'} = make_cb(recovery_cb => sub {
	my %params = @_;
	die join("\n", @{$params{'errors'}}) if ($params{'errors'});
	print "result: $params{result}\n";
    });

    # ...

    $clerk->quit(finished_cb => sub {
	Amanda::MainLoop::quit();
    });

=head1 OVERVIEW

This package is the counterpart to L<Amanda::Recovery::Scribe>, and handles
re-assembling dumpfiles from multiple parts, possibly distributed over several
volumes.

A Clerk manages a source element in a transfer.  The destination is up to the
caller - it could be a file, or even an element obtained from a Scribe.  A
particular Clerk instance only handles one transfer at a time, but maintains
its state between transfers, so multiple transfers from the same device will
proceed without rewinding or reloading the volume.

At a high level, the Clerk is operated as follows: the caller provides a dump,
which includes information on the volumes, and file numbers on those volumes,
from which to read the data.  The dump object is from L<Amanda::DB::Catalog>,
usually by awy of L<Amanda::Recovery::Planner>.  The Clerk responds with a
transfer source element, which the caller then uses to construct an start a
transfer.  The clerk then uses a changer to find the required volumes, seeks to
the appropriate files, and reads the data into the transfer.  Note that the
clerk can also recover holding-disk files.

Because the clerk operates transfers (see L<Amanda::Xfer>) and the Changer API
(L<Amanda::Changer>), its operations assume that L<Amanda::MainLoop> is in use.

=head1 OPERATING A CLERK

To use a Clerk, first create a new object, giving the changer object that the
Clerk should use to load devices:

  my $clerk = Amanda::Recovery::Clerk->new(
	scan => $scan);

If the optional parameter C<debug> is given with a true value, then the Clerk
will log additional debug information to the Amanda debug logs.  This value is
also set to true if the C<DEBUG_RECOVERY> configuration parameter is set.

The optional C<feedback> parameter gives an object which will handle feedback
form the clerk.  See FEEDBACK, below.

The C<scan> parameter must be an L<Amanda::Recover::Scan> instance, which
will be used to find the volumes required for the recovery.

=head2 TRANSFERRING A DUMPFILE

Next, get a dump object and supply it to the Clerk to get a transfer source
element.  The Clerk will verify that the supplied dump matches the on-medium
header during the recovery operation, taking into account the C<single_part>
flag added by the Planner for single-part recoveries.

  $clerk->get_xfer_src(
	dump => $dump,
	xfer_src_cb => $xfer_src_cb);

During this operation, the Clerk looks up the first part in the dump and
fetches its header.  Callers often need this header to construct a transfer
appropriate to the data on the volume.  The C<$xfer_src_cb> is called with a
transfer element and with the first header, or with a list of errors if
something goes wrong.  The final argument is true if the device from which
the restore is done supports directtcp.

    $xfer_src_cb->(undef, $header, $xfer_src, $dtcp_supp); # OK
    $xfer_src_cb->([ $err, $err2 ], undef, undef, undef); # errors

Once C<$xfer_src_cb> has been called, build the transfer element into a
transfer, and start the transfer.  Send all transfer messages to the clerk:

  my $xfer->start(sub {
    my ($src, $msg, $xfer) = @_;
    $clerk->handle_xmsg($src, $msg, $xfer);
  });

Once the transfer is started, inform the Clerk:

  $clerk->recovery_started(
    xfer => $xfer,
    recovery_cb => $recovery_cb);

The C<$recovery_cb> will be called when the recovery is complete - either
successfully or unsuccessfully.  It is called as:

    $recovery_cb->(
	result => "DONE", # or "FAILED"
	errors => [], # or a list of error messages
    );

Once the recovery callback has been invoked, it is safe to start a new transfer
with the same Clerk.

Note that, because the Clerk only handles one transfer at a time, if dumpfiles
are interleaved on a volume then the recovery process will need to seek to and
read all parts from one dumpfile before reading any parts from the next
dumpfile.  Amanda does not generate interleaved dumps, so in practice this
limitation is not significant.

=head2 QUITTING

When all necessary dumpfiles have been transferred, the Clerk must be cleanly
shut down.  This is done with the C<quit> method:

  $clerk->quit(
    finished_cb => $finished_cb);

The process should not exit until the C<finished_cb> has been invoked.

=head2 FEEDBACK

A feedback object implements a number of methods that are called at various
times in the recovery process, allowing the user to customize that behavior.  A
user-defined feedback object should inherit from
C<Amanda::Recovery::Clerk::Feedback>, which implements no-op versions of all of
the methods.

The C<notif_part> method is called just before each part is restored, and is
given the label, filenum, and header.  Its return value, if any, is ignored.
Similarly, C<notif_holding> is called for a holding-disk recovery and is given
the holding filename and its header.  Note that C<notif_holding> is called
before the C<xfer_src_cb>, since data will begin flowing from a holding disk
immediately when the transfer is started.

A typical Clerk feedback class might look like:

    use base 'Amanda::Recovery::Clerk::Feedback';

    sub part_notif {
	my $self = shift;
	my ($label, $filenum, $hdr) = @_;
	print "restoring part ", $hdr->{'partnum'},
	      " from '$label' file $filenum\n";
    }


=cut

sub new {
    my $class = shift;
    my %params = @_;

    my $debug = $Amanda::Config::debug_recovery;
    $debug = $params{'debug'}
	if defined $params{'debug'} and $params{'debug'} > $debug;

    my $self = {
	scan => $params{'scan'},
	debug => $debug,
	feedback => $params{'feedback'}
	    || Amanda::Recovery::Clerk::Feedback->new(),

	current_label => undef,
	current_dev => undef,
	current_res => undef,

	xfer_state => undef,
    };

    return bless ($self, $class);
}

sub get_xfer_src {
    my $self = shift;
    my %params = @_;

    for my $rq_param qw(dump xfer_src_cb) {
	croak "required parameter '$rq_param' missing"
	    unless exists $params{$rq_param};
    }

    die "Clerk is already busy" if $self->{'xfer_state'};

    # set up a new xfer_state
    my $xfer_state = $self->{'xfer_state'} = {
	dump => $params{'dump'},
	is_holding => exists $params{'dump'}->{'parts'}[1]{'holding_file'},
	next_part_idx => 1,
	next_part => undef,

	xfer_src => undef,
	xfer => undef,
	xfer_src_ready => 0,

	recovery_cb => undef,
	xfer_src_cb => $params{'xfer_src_cb'},

	writing_part => 0,

	errors => [],
    };

    $self->_maybe_start_part();
}

sub start_recovery {
    my $self = shift;
    my %params = @_;

    $self->dbg("starting recovery");
    for my $rq_param qw(xfer recovery_cb) {
	croak "required parameter '$rq_param' missing"
	    unless exists $params{$rq_param};
    }

    die "no xfer is in progress" unless $self->{'xfer_state'};
    die "get_xfer_src has not finished"
	if defined $self->{'xfer_state'}->{'xfer_src_cb'};

    my $xfer_state = $self->{'xfer_state'};
    $xfer_state->{'recovery_cb'} = $params{'recovery_cb'};
    $xfer_state->{'xfer'} = $params{'xfer'};

    $self->_maybe_start_part();
}

sub handle_xmsg {
    my $self = shift;
    my ($src, $msg, $xfer) = @_;

    if ($msg->{'elt'} == $self->{'xfer_state'}->{'xfer_src'}) {
	if ($msg->{'type'} == $XMSG_PART_DONE) {
	    $self->_xmsg_part_done($src, $msg, $xfer);
	} elsif ($msg->{'type'} == $XMSG_ERROR) {
	    $self->_xmsg_error($src, $msg, $xfer);
	} elsif ($msg->{'type'} == $XMSG_READY) {
	    $self->_xmsg_ready($src, $msg, $xfer);
	}
    }

    if ($msg->{'type'} == $XMSG_DONE) {
	$self->_xmsg_done($src, $msg, $xfer);
    }
}

sub quit {
    my $self = shift;
    my %params = @_;

    die "Cannot quit a Clerk while a transfer is in progress"
	if $self->{'xfer_state'};

    # if we have a reservation, we need to release it; otherwise, we can
    # just call finished_cb
    if ($self->{'current_res'}) {
	$self->{'current_dev'}->finish();
	$self->{'current_res'}->release(finished_cb => $params{'finished_cb'});
    } else {
	$params{'finished_cb'}->();
    }
}

sub _xmsg_ready {
    my $self = shift;
    my ($src, $msg, $xfer) = @_;
    my $xfer_state = $self->{'xfer_state'};

    if (!$xfer_state->{'is_holding'}) {
	$xfer_state->{'xfer_src_ready'} = 1;
	$self->_maybe_start_part();
    }
}

sub _xmsg_part_done {
    my $self = shift;
    my ($src, $msg, $xfer) = @_;
    my $xfer_state = $self->{'xfer_state'};

    my $next_label = $xfer_state->{'next_part'}->{'label'};
    my $next_filenum = $xfer_state->{'next_part'}->{'filenum'};

    die "read incorrect filenum"
	unless $next_filenum == $msg->{'fileno'};
    $self->dbg("done reading file $next_filenum on '$next_label'");

    # fix up the accounting, and then see if we can do something else
    shift @{$xfer_state->{'remaining_plan'}};
    $xfer_state->{'next_part_idx'}++;
    $xfer_state->{'next_part'} = undef;

    $self->_maybe_start_part();
}

sub _xmsg_error {
    my $self = shift;
    my ($src, $msg, $xfer) = @_;
    my $xfer_state = $self->{'xfer_state'};

    push @{$xfer_state->{'errors'}}, $msg->{'message'};
}

sub _xmsg_done {
    my $self = shift;
    my ($src, $msg, $xfer) = @_;
    my $xfer_state = $self->{'xfer_state'};

    # eliminate the transfer's state, since it's done
    $self->{'xfer_state'} = undef;

    # note that this does not release the reservation, in case the next
    # transfer is from the same volume
    my $result = (@{$xfer_state->{'errors'}})? "FAILED" : "DONE";
    return $xfer_state->{'recovery_cb'}->(
	result => $result,
	errors => $xfer_state->{'errors'},
    );
}

sub _maybe_start_part {
    my $self = shift;
    my %subs;

    my $xfer_state = $self->{'xfer_state'};

    # if we're still working on a part, do nothing
    return if $xfer_state->{'writing_part'};

    # NOTE: this is invoked *both* from get_xfer_src and start_recovery;
    # in the former case it merely loads the file and returns the header.

    # if we have an xfer source already, and it's not ready, then don't start
    # the part.  This happens when start_recovery is called before XMSG_READY.
    return if $xfer_state->{'xfer_src'} and not $xfer_state->{'xfer_src_ready'};

    # if we have an xfer source already, but the recovery hasn't started, then
    # don't start the part.  This happens when XMSG_READY comes before
    # start_recovery.
    return if $xfer_state->{'xfer_src'} and not $xfer_state->{'recovery_cb'};

    $subs{'check_next'} = make_cb(check_next => sub {
	# first, see if anything remains to be done
	if (!exists $xfer_state->{'dump'}{'parts'}[$xfer_state->{'next_part_idx'}]) {
	    # this should not happen until the xfer is started..
	    die "xfer should be running already"
		unless $xfer_state->{'xfer'};
	    # tell the source to generate EOF
	    $xfer_state->{'xfer_src'}->start_part(undef);
	    return;
	}

	$xfer_state->{'next_part'} =
	    $xfer_state->{'dump'}{'parts'}[$xfer_state->{'next_part_idx'}];

	# short-circuit for a holding disk
	if ($xfer_state->{'is_holding'}) {
	    return $subs{'holding_recovery'}->();
	}

	my $next_label = $xfer_state->{'next_part'}->{'label'};
	# load the next label, if necessary
	if ($self->{'current_label'} and
	     $self->{'current_label'} eq $next_label) {
	    # jump to the seek_file call
	    return $subs{'seek_and_check'}->();
	}

	# need to get a new tape
	return $subs{'release'}->();
    });

    $subs{'release'} = make_cb(release => sub {
	if (!$self->{'current_res'}) {
	    return $subs{'released'}->();
	}

	$self->{'current_dev'}->finish();
	$self->{'current_res'}->release(
		finished_cb => $subs{'released'});
    });

    $subs{'released'} = make_cb(released => sub {
	my ($err) = @_;

	if ($err) {
	    push @{$xfer_state->{'errors'}}, "$err";
	    return $subs{'handle_error'}->();
	}

	$self->{'current_dev'} = undef;
	$self->{'current_res'} = undef;
	$self->{'current_label'} = undef;

	# now load the next volume

	my $next_label = $xfer_state->{'next_part'}->{'label'};

	$self->dbg("loading volume '$next_label'");
	$self->{'scan'}->find_volume(label => $next_label,
			res_cb => $subs{'loaded_label'});
    });

    $subs{'loaded_label'} = make_cb(loaded_label => sub {
	my ($err, $res) = @_;

	my $next_label = $xfer_state->{'next_part'}->{'label'};

	if ($err) {
	    push @{$xfer_state->{'errors'}}, "$err";
	    return $subs{'handle_error'}->();
	}

	$self->{'current_res'} = $res;

	# open the device and check the label, then go to seek_and_check
	if (!$res->{'device'}->start($Amanda::Device::ACCESS_READ, undef, undef)) {
	    $err = $res->{'device'}->error_or_status();
	} else {
	    if ($res->{'device'}->volume_label ne $next_label) {
		$err = "expected volume label '$next_label', but found volume " .
		       "label '" . $res->{'device'}->volume_label . "'";
	    } else {
		$self->{'current_dev'} = $res->{'device'};
		$self->{'current_label'} = $res->{'device'}->volume_label;

		# success!
		return $subs{'seek_and_check'}->();
	    }
	}

	# the volume didn't work out, so release the reservation and fail
	$res->release(finished_cb => sub {
	    my ($release_err) = @_;

	    if ($release_err) { # geez, someone is having a bad day!
		push @{$xfer_state->{'errors'}}, "$release_err";
		return $subs{'handle_error'}->();
	    }

	    push @{$xfer_state->{'errors'}}, "$err";
	    return $subs{'handle_error'}->();
	});
    });

    $subs{'seek_and_check'} = make_cb(seek_and_check => sub {
	my $next_label = $xfer_state->{'next_part'}->{'label'};
	my $next_filenum = $xfer_state->{'next_part'}->{'filenum'};
	my $dev = $self->{'current_dev'};
	my $on_vol_hdr = $dev->seek_file($next_filenum);

	if (!$on_vol_hdr) {
	    push @{$xfer_state->{'errors'}}, $dev->error_or_status();
	    return $subs{'handle_error'}->();
	}

	if (!$self->_header_expected($on_vol_hdr)) {
	    # _header_expected already pushed an error message or two
	    return $subs{'handle_error'}->();
	}

	# now, either start the part, or invoke the xfer_src_cb.
	if ($xfer_state->{'xfer_src_cb'}) {
	    my $cb = $xfer_state->{'xfer_src_cb'};
	    $xfer_state->{'xfer_src_cb'} = undef;

	    # make a new xfer_source
	    $xfer_state->{'xfer_src'} = Amanda::Xfer::Source::Recovery->new($dev),
	    $xfer_state->{'xfer_src_ready'} = 0;

	    $self->dbg("successfully located first part for recovery");
	    return $cb->(undef, $on_vol_hdr, $xfer_state->{'xfer_src'},
			    $dev->directtcp_supported());
	} else {
	    # notify caller of the part
	    $self->{'feedback'}->notif_part($next_label, $next_filenum, $on_vol_hdr);

	    $self->dbg("reading file $next_filenum on '$next_label'");
	    return $xfer_state->{'xfer_src'}->start_part($dev);
	}
    });

    # ---

    # handle a holding restore
    $subs{'holding_recovery'} = make_cb(holding_recovery => sub {
	my $next_filename = $xfer_state->{'next_part'}->{'holding_file'};
	my $on_disk_hdr = Amanda::Holding::get_header($next_filename);

	if (!$on_disk_hdr) {
	    push @{$xfer_state->{'errors'}}, "error loading header from '$next_filename'";
	    return $subs{'handle_error'}->();
	}

	if (!$self->_header_expected($on_disk_hdr)) {
	    # _header_expected already pushed an error message or two
	    return $subs{'handle_error'}->();
	}

	# now invoke the xfer_src_cb if it hasn't already been called.
	if ($xfer_state->{'xfer_src_cb'}) {
	    my $cb = $xfer_state->{'xfer_src_cb'};
	    $xfer_state->{'xfer_src_cb'} = undef;

	    $xfer_state->{'xfer_src'} = Amanda::Xfer::Source::Holding->new(
			$xfer_state->{'dump'}->{'parts'}[1]{'holding_file'}),

	    # Amanda::Xfer::Source::Holding was *born* ready.
	    $xfer_state->{'xfer_src_ready'} = 1;

	    # notify caller of the part, *before* xfer_src_cb is called!
	    $self->{'feedback'}->notif_holding($next_filename, $on_disk_hdr);

	    $self->dbg("successfully located holding file for recovery");
	    return $cb->(undef, $on_disk_hdr, $xfer_state->{'xfer_src'}, 0);
	}
	# (nothing to do until the xfer is done)
    });


    # ----

    # this utility sub handles errors differently depending on which phase is active.
    $subs{'handle_error'} = make_cb(handle_error => sub {
	if ($xfer_state->{'xfer_src_cb'}) {
	    # xfer_src_cb hasn't been called yet, so invoke it now,
	    # after deleting the xfer state
	    $self->{'xfer_state'} = undef;

	    return $xfer_state->{'xfer_src_cb'}->($xfer_state->{'errors'},
			undef, undef, undef);
	} else {
	    # cancelling the xfer will eventually invoke recovery_cb
	    # via the XMSG_DONE
	    return $xfer_state->{'xfer'}->cancel();
	}
    });

    $subs{'check_next'}->();
}

sub _header_expected {
    my $self = shift;
    my ($on_vol_hdr) = @_;
    my $xfer_state = $self->{'xfer_state'};
    my $next_part = $xfer_state->{'next_part'};
    my @errs;

    if ($on_vol_hdr->{'name'} ne $next_part->{'dump'}->{'hostname'}) {
	push @errs, "got hostname '$on_vol_hdr->{name}'; " .
		    "expected '$next_part->{dump}->{hostname}'";
    }
    if ($on_vol_hdr->{'disk'} ne $next_part->{'dump'}->{'diskname'}) {
	push @errs, "got disk '$on_vol_hdr->{disk}'; " .
		    "expected '$next_part->{dump}->{diskname}'";
    }
    if ($on_vol_hdr->{'datestamp'} ne $next_part->{'dump'}->{'dump_timestamp'}) {
	push @errs, "got datestamp '$on_vol_hdr->{datestamp}'; " .
		    "expected '$next_part->{dump}->{dump_timestamp}'";
    }
    if ($on_vol_hdr->{'dumplevel'} != $next_part->{'dump'}->{'level'}) {
	push @errs, "got dumplevel '$on_vol_hdr->{dumplevel}'; " .
		    "expected '$next_part->{dump}->{level}'";
    }
    unless ($xfer_state->{'is_holding'}) {
	if ($on_vol_hdr->{'partnum'} != $next_part->{'partnum'}) {
	    push @errs, "got partnum '$on_vol_hdr->{partnum}'; " .
			"expected '$next_part->{partnum}'";
	}
    }

    if (@errs) {
	my $errmsg;
	if ($xfer_state->{'is_holding'}) {
	    $errmsg = "header on '$next_part->{holding_file}' does not match expectations: ";
	} else {
	    my $label = $next_part->{'label'};
	    my $filenum = $next_part->{'filenum'};
	    $errmsg = "header on '$label' file $filenum does not match expectations: ";
	}
	$errmsg .= join("; ", @errs);
	push @{$xfer_state->{'errors'}}, $errmsg;
	return 0;
    }
    return 1;
}

sub dbg {
    my ($self, $msg) = @_;
    if ($self->{'debug'}) {
	debug("Amanda::Recovery::Clerk: $msg");
    }
}

package Amanda::Recovery::Clerk::Feedback;

sub new {
    return bless {}, shift;
}

sub notif_part { }

sub notif_holding { }

1;
