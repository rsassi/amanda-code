# Copyright (c) 2009-2015 Zmanda, Inc.  All Rights Reserved.
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
# Contact information: Zmanda Inc., 465 S Mathlida Ave, Suite 300
# Sunnyvale, CA 94086, USA, or: http://www.zmanda.com

use strict;
use warnings;

=head1 PROTOCOl
  server (amfetchdump)	       client (restore)

                            <= FEATURES string
  DIRECTORY directory	 =>
  HEADER-SEND-SIZE size	 =>				# fe_restore_header_send_size
			    <= HEADER-READY		# fe_restore_header_ready
  header (size:data)	 =>
			    <= HEADER-DONE		# fe_restore_header_done
			       run application support command
  STATE-SEND		 =>				# fe_restore_state_send
			    <= STATE-READY		# fe_restore_state_ready
  statefile (EOF:state)	 =>
			    <= STATE-DONE		# fe_restore_state_done
  USE-DAR (YES|NO)	 =>				# fe_restore_dar
			    <= USE-DAR (YES|NO)		# fe_restore_dar
  AVAIL-DATAPATH AMANDA	 =>				# fe_restore_datapath
			    <= USE-DATAPATH AMANDA	# fe_restore_datapath
  DATAPATH-OK		 =>				# fe_restore_datapath
  DATA-SEND		 =>				# fe_restore_data_send
			       run application restore command
			    <= DATA-READY		# fe_restore_data_ready
			    <= DAR x:y			# fe_restore_dar and USE-DAR is YES
  data (EOF:data)	 =>
			    <= DAR-DONE			# fe_restore_dar and USE-DAR is YES
			    #<= DATA-DONE		# fe_restore_data_done

=head1 FEATURES

=cut

package Amanda::Service::Restore;

##
# ClientService class

use vars qw( @ISA );
use Amanda::ClientService;
use Amanda::Recovery::Clerk;
@ISA = qw( Amanda::ClientService Amanda::Recovery::Clerk::Feedback);

use Sys::Hostname;
use IPC::Open2;
use JSON -convert_blessed_universally;

use Amanda::Debug qw( debug info warning );
use Amanda::MainLoop qw( :GIOCondition );
use Amanda::Util qw( :constants match_disk match_host );
use Amanda::Feature;
use Amanda::Config qw( :init :getconf );
use Amanda::Storage;
use Amanda::Changer;
use Amanda::Recovery::Scan;
use Amanda::Xfer qw( :constants );
use Amanda::Cmdline;
use Amanda::Recovery::Clerk;
use Amanda::Recovery::Planner;
use Amanda::Recovery::Scan;
use Amanda::DB::Catalog;
use Amanda::Disklist;
use Amanda::Restore;

# Note that this class performs its control IO synchronously.  This is adequate
# for this service, as it never receives unsolicited input from the remote
# system.

sub run {
    my $self = shift;

    $self->{'my_features'} = Amanda::Feature::Set->mine();
    $self->{'their_features'} = Amanda::Feature::Set->old();
    $self->{'all_filter'} = {};

    $self->setup_streams();
}

sub set_feedback {
}

sub user_message {
    my $self = shift;
    my $message = shift;

    debug("user_message feedback: $message");
    $self->sendmessage($message);
}

sub setup_streams {
    my $self = shift;

    my $req = $self->get_req();

    # make some sanity checks
    my $errors = [];
    if (defined $req->{'options'}{'auth'} and defined $self->amandad_auth()
		and $req->{'options'}{'auth'} ne $self->amandad_auth()) {
	my $reqauth = $req->{'options'}{'auth'};
	my $amauth = $self->amandad_auth();
	push @$errors, "recover program requested auth '$reqauth', " .
		       "but amandad is using auth '$amauth'";
	$main::exit_status = 1;
    }

    # and pull out the features, if given
    if (defined($req->{'options'}{'features'})) {
	$self->{'their_features'} = Amanda::Feature::Set->from_string($req->{'options'}{'features'});
    }

    if ($self->{'their_features'}->has($Amanda::Feature::fe_restore_state_stream)) {
	$self->send_rep(['CTL' => 'rw', 'DATA' => 'r', 'MESG' => 'w', 'STATE' => 'r'], $errors);
	$self->{'state_stream' } = 'STATE';
    } else {
	$self->send_rep(['CTL' => 'rw', 'DATA' => 'r', 'MESG' => 'w'], $errors);
    }

    return $self->quit() if (@$errors);

    $self->{'ctl_stream' } = 'CTL';
    $self->{'data_stream'} = 'DATA';
    $self->{'mesg_stream'} = 'MESG';

    if ($req->{'options'}->{'config'}) {
	config_init($CONFIG_INIT_CLIENT | $CONFIG_INIT_EXPLICIT_NAME | $CONFIG_INIT_OVERLAY,
                            $req->{'options'}->{'config'});
	my ($cfgerr_level, @cfgerr_errors) = config_errors();
	if ($cfgerr_level >= $CFGERR_ERRORS) {
	    die "configuration errors; aborting connection";
	}
	Amanda::Util::finish_setup($RUNNING_AS_DUMPUSER_PREFERRED);
    }

    $self->sendctlline("FEATURES " . $self->{'my_features'}->as_string() . "\r\n");
    $self->read_command();
}

sub read_command {
    my $self = shift;
    my $ctl_stream = $self->{'ctl_stream'};
    my $command = $self->{'command'} = {};

    my $line = $self->getline($ctl_stream);
    $line =~ s/\r?\n$//g;
    if ($line !~ /^DIRECTORY (.*)/) {
	chomp $line;
	chop $line;
	$self->user_message(Amanda::FetchDump::Message->new(
			source_filename => __FILE__,
			source_line     => __LINE__,
			code            => 3300064,
			severity        => $Amanda::Message::ERROR,
			expect          => "DIRECTORY",
			line            => $line));
	exit 1;
    }
    $self->{'directory'} = Amanda::Util::unquote_string($1);

    ($self->{'restore'}, my $result_message) = Amanda::Restore->new();
    $self->{'restore'}->restore(
		'source-fd'	 => $self->rfd($self->{'data_stream'}),
		'directory'	 => $self->{'directory'},
		'extract'	 => 1,
		'decompress'     => 1,
		'decrypt'        => 1,
		'feedback'	 => $self,
		'their_features' => $self->{'their_features'},
		'finished_cb'	 => sub {
					$main::exit_status = shift;
					$self->quit();
				       });

    return;
}

sub get_header {
    my $self = shift;

    $self->{'header-size'} = 32768;

    if ($self->{'their_features'}->has($Amanda::Feature::fe_restore_header_send_size)) {
	my $line = $self->getline('CTL');
	$line =~ s/\r?\n$//g;
	if ($line !~ /^HEADER-SEND-SIZE (.*)/) {
	    chomp $line;
	    chop $line;
	    return Amanda::FetchDump::Message->new(
			source_filename => __FILE__,
			source_line     => __LINE__,
			code            => 3300064,
			severity        => $Amanda::Message::ERROR,
			expect          => "HEADER-SEND-SIZE",
			line            => $line);
	}
	$self->{'header-size'} = 0 + $1;
    }
    if ($self->{'their_features'}->has($Amanda::Feature::fe_restore_header_ready)) {
	$self->sendctlline("HEADER-READY\r\n");
    }

    # read header from DATA
    my $header = Amanda::Util::full_read($self->rfd($self->{'data_stream'}), $self->{'header-size'});

    # print HEADER-DONE to CTL
    $self->sendctlline("HEADER-DONE\r\n");

    # parse header
    $self->{'hdr'} = Amanda::Header->from_string($header);

    return $self->{'hdr'};
}

sub send_dar_data {
    my $self = shift;
    my $line = shift;

    chomp $line;
    $self->sendctlline("$line\r\n");
    return undef;
}

sub transmit_state_file {
    my $self = shift;
    my $header = shift;

    return if !$self->{'their_features'}->has($Amanda::Feature::fe_restore_state_stream);

    if ($self->{'their_features'}->has($Amanda::Feature::fe_restore_state_send)) {
	my $line = $self->getline('CTL');
	$line =~ s/\r?\n$//g;
	if ($line =~ /^NO-STATE-SEND/) {
	    $self->close($self->{'state_stream'}, 'r');
	    return;
	}
	if ($line !~ /^STATE-SEND/) {
	    chomp $line;
	    chop $line;
	    return Amanda::FetchDump::Message->new(
			source_filename => __FILE__,
			source_line     => __LINE__,
			code            => 3300064,
			severity        => $Amanda::Message::ERROR,
			expect          => "STATE-SEND",
			line            => $line);
	}
    }

    # print STATE-READY to CTL
    if ($self->{'their_features'}->has($Amanda::Feature::fe_restore_state_ready)) {
	$self->sendctlline("STATE-READY\r\n");
    }

    my $host = Amanda::Util::sanitise_filename("" . $header->{'name'});
    my $disk = Amanda::Util::sanitise_filename("" . $header->{'disk'});
    my $state_filename = getconf($CNF_TMPDIR) . '/' . $host .
                '-' . $disk . '-' . $header->{'datestamp'} . '_' .
                $header->{'dumplevel'} . '.state';
    open (STATEFILE, '>', $state_filename) || die("ERR");
    my $block;
    my $length;
    while ($block = Amanda::Util::full_read($self->rfd($self->{'state_stream'}),
				     32768)) {
	Amanda::Util::full_write(fileno(STATEFILE),
				 $block, length($block))
		or die "writing to $state_filename: $!";
    }
    close(STATEFILE);
    $self->close($self->{'state_stream'}, 'r');
    $self->{'state_filename'} = $state_filename;

    # print STATE-DONE to CTL
    if ($self->{'their_features'}->has($Amanda::Feature::fe_restore_state_done)) {
	$self->sendctlline("STATE-DONE\r\n");
    }
    return undef;
}

sub set {
    my $self = shift;
    my $hdr = shift;;
    my $dle = shift;;
    my $application_property = shift;;

    $self->{'hdr'} = $hdr;
    $self->{'dle'} = $dle;
    $self->{'application_property'} = $application_property;

    $self->{'extract'} = Amanda::Extract->new(hdr => $hdr, dle => $dle);
    die("$self->{'extract'}") if $self->{'extract'}->isa('Amanda::Message');
    ($self->{'bsu'}, my $err) = $self->{'extract'}->BSU();
    if (@$err) {
        die("BSU err " . join("\n", @$err));
    }
    return undef;
}

sub get_xfer_dest {
    my $self = shift;

    $self->{'extract'}->set_restore_argv(
		directory => $self->{'directory'},
		use_dar   => $self->{'use_dar'},
		state_filename => $self->{'state_filename'},
		application_property => $self->{'application_property'});

    if ($self->{'use_directtcp'}) {
	$self->{'xfer_dest'} = Amanda::Xfer::Dest::DirectTCPListen->new();
    } else {
	$self->{'xfer_dest'} = Amanda::Xfer::Dest::Application->new($self->{'extract'}->{'restore_argv'}, 0, 0, 0, 1);
    }

    return $self->{'xfer_dest'};
}

sub new_dest_fh {
    my $self = shift;

    my $new_dest_fh= \*STDOUT;
    return $new_dest_fh;
}

sub transmit_dar {
    my $self = shift;
    my $use_dar = shift;

    return 0 if !$self->{'their_features'}->has($Amanda::Feature::fe_restore_dar);

    my $line = $self->getline($self->{'ctl_stream'});
    my $darspec = ($line =~ /^USE-DAR (.*)\r?\n$/);
    if ($darspec ne "YES" && $darspec ne "NO") {
	chomp $line;
	chop $line;
	return Amanda::FetchDump::Message->new(
			source_filename => __FILE__,
			source_line     => __LINE__,
			code            => 3300064,
			severity        => $Amanda::Message::ERROR,
			expect          => "USE-DAR [YES|NO]",
			line            => $line);
    }
    $use_dar &= ($1 eq 'YES');
    $use_dar &= $self->{'bsu'}->{'dar'};

    if ($use_dar) {
	$self->sendctlline("USE-DAR YES\r\n");
    } else {
	$self->sendctlline("USE-DAR NO\r\n");
    }

    return $use_dar;
}

sub notify_start_backup {
    my $self = shift;

    if ($self->{'their_features'}->has($Amanda::Feature::fe_restore_data_send)) {
        my $line = $self->getline($self->{'ctl_stream'});
        if ($line ne "DATA-SEND\r\n") {
	    chomp $line;
	    chop $line;
	    return Amanda::FetchDump::Message->new(
			source_filename => __FILE__,
			source_line     => __LINE__,
			code            => 3300064,
			severity        => $Amanda::Message::ERROR,
			expect          => "DATA-SEND",
			line            => $line);
        }
    }

    if ($self->{'their_features'}->has($Amanda::Feature::fe_restore_data_ready)) {
        $self->sendctlline("DATA-READY\r\n");
    }

    return undef;
}

sub start_read_dar {
    my $self = shift;
    my $xfer_dest = shift;
    my $cb_data = shift;
    my $cb_done = shift;
    my $text = shift;

    my $fd = $xfer_dest->get_dar_fd();
    $fd.="";
    $fd = int($fd);
    my $src = Amanda::MainLoop::fd_source($fd,
                                          $G_IO_IN|$G_IO_HUP|$G_IO_ERR);
    my $buffer = "";
    $self->{'fetchdump'}->{'all_filter'}->{$src} = 1;
    $src->set_callback( sub {
	my $b;
	my $n_read = POSIX::read($fd, $b, 1);
	if (!defined $n_read) {
	    return;
	} elsif ($n_read == 0) {
	    delete $self->{'fetchdump'}->{'all_filter'}->{$src};
	    $cb_data->("DAR -1:0");
	    $src->remove();
	    POSIX::close($fd);
	    if (!%{$self->{'fetchdump'}->{'all_filter'}} and $self->{'recovery_done'}) {
		$cb_done->();
	    }
	} else {
	    $buffer .= $b;
	    if ($b eq "\n") {
		my $line = $buffer;
		chomp $line;
		if (length($line) > 1) {
		    $cb_data->($line);
		}
		$buffer = "";
	    }
	}
    });
    return undef;
}

sub get_datapath {
    my $self = shift;

    $self->{'datapath'} = 'none';

    if (!$self->{'their_features'}->has($Amanda::Feature::fe_restore_datapath)) {
	$self->{'datapath'} = 'amanda';
	return;
    }


    my $line = $self->getline($self->{'ctl_stream'});
    my ($dpspec) = ($line =~ /^AVAIL-DATAPATH (.*)\r\n$/);
    if (!defined $dpspec) {
	chomp $line;
	chop $line;
	return Amanda::FetchDump::Message->new(
			source_filename => __FILE__,
			source_line     => __LINE__,
			code            => 3300064,
			severity        => $Amanda::Message::ERROR,
			expect          => "AVAIL-DATAPATH",
			line            => $line);
    }
    my @avail_dps = split / /, $dpspec;

    if (grep /^DIRECT-TCP$/, @avail_dps) {
	# remote can handle a directtcp transfer .. can we?
	# BUG: Must check application BSU
	if ($self->{'xfer_src_supports_directtcp'}) {
	    $self->{'datapath'} = 'directtcp';
	} else {
	    $self->{'datapath'} = 'amanda';
	}
    } else {
	# remote can at least handle AMANDA
	die "remote cannot handle AMANDA datapath??"
	    unless grep /^AMANDA$/, @avail_dps;
	$self->{'datapath'} = 'amanda';
    }
    return undef;
}

sub send_amanda_datapath {
    my $self = shift;

    return if !$self->{'their_features'}->has($Amanda::Feature::fe_restore_datapath);

    if ($self->{'datapath'} eq 'amanda') {
	$self->sendctlline("USE-DATAPATH AMANDA\r\n");
	my $line = $self->getline($self->{'ctl_stream'});
	if ($line !~ /^DATAPATH-OK\r?$/) {
	    chomp $line;
	    chop $line;
	    return Amanda::FetchDump::Message->new(
			source_filename => __FILE__,
			source_line     => __LINE__,
			code            => 3300064,
			severity        => $Amanda::Message::ERROR,
			expect          => "DATAPATH-OK",
			line            => $line);
	}
    }
    return undef;
}

sub send_directtcp_datapath {
    my $self = shift;

    return if !$self->{'their_features'}->has($Amanda::Feature::fe_restore_datapath);

    # send the data-path response, if we have a datapath
    if ($self->{'datapath'} eq 'directtcp') {
	my $addrs = $self->{'fetchdump'}->{'xfer_dest'}->get_addrs();
	$addrs = [ map { $_->[0] . ":" . $_->[1] } @$addrs ];
	$addrs = join(" ", @$addrs);
	$self->sendctlline("USE-DATAPATH DIRECT-TCP $addrs\r\n");
	my $line = $self->getline($self->{'ctl_stream'});
	if ($line !~ /^DATAPATH-OK\r?$/) {
	    chomp $line;
	    chop $line;
	    return Amanda::FetchDump::Message->new(
			source_filename => __FILE__,
			source_line     => __LINE__,
			code            => 3300064,
			severity        => $Amanda::Message::ERROR,
			expect          => "DATAPATH-OK",
			line            => $line);
	}
    }
    return undef;
}

sub quit {
    my $self = shift;

    if ($self->{'clerk'}) {
	$self->{'clerk'}->quit(finished_cb => sub {
	    my ($err) = @_;
	    $self->{'chg'}->quit() if defined $self->{'chg'};
	    if ($err) {
		# it's *way* too late to report this to amrecover now!
		warning("while quitting clerk: $err");
	    }
	    $self->quit1();
	});
    } else {
	$self->{'scan'}->quit() if defined $self->{'scan'};
	$self->{'chg'}->quit() if defined $self->{'chg'};
	$self->quit1();
    }

}

sub quit1 {
    my $self = shift;

    $self->{'storage'}->quit() if defined($self->{'storage'});
    $self->{'fetch_done'} = 1;
    if (!%{$self->{'all_filter'}}) {
	Amanda::MainLoop::quit();
    }
}

## utilities

sub get_req {
    my $self = shift;

    my $req_str = '';
    while (1) {
	my $buf = Amanda::Util::full_read($self->rfd('main'), 1024);
	last unless $buf;
	$req_str .= $buf;
    }
    # we've read main to EOF, so close it
    $self->close('main', 'r');

    $self->{'req'} = $self->parse_req($req_str);
    return $self->{'req'};
}

sub send_rep {
    my $self = shift;
    my ($streams, $errors) = @_;
    my $rep = '';

    # first, if there were errors in the REQ, report them
    if (@$errors) {
	for my $err (@$errors) {
	    $rep .= "ERROR $err\n";
	}
    } else {
	my $connline = $self->connect_streams(@$streams);
	$rep .= "$connline\n";
    }

    $rep .= "OPTIONS ";
    if ($self->{'their_features'}->has($Amanda::Feature::fe_rep_options_features)) {
        $rep .= 'features=' . $self->{'my_features'}->as_string() . ';';
    }
    if ($self->{'their_features'}->has($Amanda::Feature::fe_rep_options_hostname)) {
        $rep .= 'hostname=' . $self->{'req'}->{'hostsname'} . ';';
    }
    if (!$self->{'their_features'}->has($Amanda::Feature::fe_rep_options_features) ||
	!$self->{'their_features'}->has($Amanda::Feature::fe_rep_options_hostname)) {
        $rep .= ";";
    }
    # rep needs a empty-line terminator, I think
    $rep .= "\n";

    # write the whole rep packet, and close main to signal the end of the packet
    $self->senddata('main', $rep);
    $self->close('main', 'w');
}

# helper function to get a line, including the trailing '\n', from a stream.  This
# reads a character at a time to ensure that no extra characters are consumed.  This
# could certainly be more efficient! (TODO)
sub getline {
    my $self = shift;
    my ($stream) = @_;
    my $fd = $self->rfd($stream);
    my $line = '';

    while (1) {
	my $c;
	my $bytes = POSIX::read($fd, $c, 1)
	    or last;
	last if $bytes == 0;
	$line .= $c;
	last if $c eq "\n";
    }

    $line =~ /^(.*)$/;
    my $chopped = $1;
    $chopped =~ s/[\r\n]*$//g;
    debug("CTL << $chopped");

    return $line;
}

# like getline, but async; TODO:
#  - make all uses of getline async
#  - use buffering to read more than one character at a time
sub getline_async {
    my $self = shift;
    my ($stream, $async_read_cb) = @_;
    my $fd = $self->rfd($stream);

    my $data_in;
    my $buf = '';

    $data_in = sub {
	my ($err, $data) = @_;

	return $async_read_cb->($err, undef) if $err;

	$buf .= $data;
	if ($buf =~ /^(.*\r\n)$/) {
	    my $chopped = $1;
	    $chopped =~ s/[\r\n]*$//g;
	    debug("CTL << $chopped");

	    $async_read_cb->(undef, $buf);
	} else {
	    Amanda::MainLoop::async_read(fd => $fd, size => 1, async_read_cb => $data_in);
	}
    };
    Amanda::MainLoop::async_read(fd => $fd, size => 1, async_read_cb => $data_in);
}

# helper function to write a data to a stream.  This does not add newline characters.
# If the callback is given, this is async (TODO: all calls should be async)
sub senddata {
    my $self = shift;
    my ($stream, $data, $async_write_cb) = @_;
    my $fd = $self->wfd($stream);

    if (defined $async_write_cb) {
	return Amanda::MainLoop::async_write(
		fd => $fd,
		data => $data,
		async_write_cb => $async_write_cb);
    } else {
	Amanda::Util::full_write($fd, $data, length($data))
	    or die "writing to $stream: $!";
    }
}

# send a line on the control stream, or just log it if the ctl stream is gone;
# async callback is just like for senddata
sub sendctlline {
    my $self = shift;
    my ($msg, $async_write_cb) = @_;

    my $chopped = $msg;
    $chopped =~ s/[\r\n]*$//g;

    if ($self->{'ctl_stream'}) {
	debug("CTL >> $chopped");
	return $self->senddata($self->{'ctl_stream'}, $msg, $async_write_cb);
    } else {
	debug("not sending CTL message as CTL is closed >> $chopped");
	if (defined $async_write_cb) {
	    $async_write_cb->(undef, length($msg));
	}
    }
}

# send a MESSAGE on the CTL stream, but only if the remote has
# fe_amrecover_message
sub sendmessage {
    my $self = shift;
    my $msg  = shift;
    my $async_write_cb  = shift;

    if (!defined $self->{'json'}) {
	$self->{'json'} = JSON->new->allow_nonref->convert_blessed;
    }
    if ($self->{'mesg_stream'}) {
	debug("MESG >> $msg");
	if ($self->{'their_features'}->has($Amanda::Feature::fe_restore_mesg_json)) {
	    return $self->senddata($self->{'mesg_stream'}, $self->{'json'}->pretty->encode($msg), $async_write_cb);
	} else {
	    return $self->senddata($self->{'mesg_stream'}, "$msg\n", $async_write_cb);
	}
    } else {
	debug("not sending MESG message as MESG is closed >> $msg");
	if (defined $async_write_cb) {
	    $async_write_cb->(undef, length($msg));
	}
    }
}

# covert a tapespec to a holding filename
sub tapespec_to_holding {
    my $self = shift;
    my ($tapespec) = @_;

    my $filelist = Amanda::Util::unmarshal_tapespec(0, $tapespec);

    # $filelist should have the form [ "HOLDING", $holding_file, [ 0 ] ]
    die "invalid holding tapespec" unless @$filelist == 3;
    die "invalid holding tapespec" unless $filelist->[0] eq "HOLDING";
    die "invalid holding tapespec" unless @{$filelist->[2]} == 1;
    die "invalid holding tapespec" unless $filelist->[2][0] == 0;

    return $filelist->[1];
}

# amrecover didn't give us much to go on, but see if we can find a dump that
# will make it happy.
sub try_to_find_dump {
    my $self = shift;
    my ($storage, $label, $spec) = @_;

    # search the catalog; get_dumps cannot search by labels, so we have to use
    # get_parts instead
    my @parts = Amanda::DB::Catalog::get_parts(
	storage => $storage,
	label => $label,
	dumpspecs => [ $spec ]);

    if (!@parts) {
	$self->sendmessage("could not find any matching dumps on volume '$label'");
	return undef;
    }

    # (note that if there is more than one dump in @parts, the planner will
    # catch it later)

    # sort the parts by their order on each volume.  This sorts the volumes
    # lexically by label, but the planner will straighten it out.
    @parts = Amanda::DB::Catalog::sort_dumps([ "label", "filenum" ], @parts);

    # loop over the parts for the dump and make a filelist.
    my $last_label = '';
    my $last_filenums = undef;
    my $filelist = [];
    for my $part (@parts) {
	next unless defined $part; # skip part number 0
	if ($part->{'label'} ne $last_label) {
	    $last_label = $part->{'label'};
	    $last_filenums = [];
	    push @$filelist, $part->{'storage'}, $last_label, $last_filenums;
	}
	push @$last_filenums, $part->{'filenum'};
    }

    return $filelist;
}

1;

