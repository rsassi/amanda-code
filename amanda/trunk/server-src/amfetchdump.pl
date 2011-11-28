#! @PERL@
# Copyright (c) 2009, 2010 Zmanda, Inc.  All Rights Reserved.
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

use lib '@amperldir@';
use strict;
use warnings;

use Getopt::Long;

use Amanda::Device qw( :constants );
use Amanda::Debug qw( :logging );
use Amanda::Config qw( :init :getconf config_dir_relative );
use Amanda::Util qw( :constants );
use Amanda::Changer;
use Amanda::Constants;
use Amanda::MainLoop;
use Amanda::Header;
use Amanda::Holding;
use Amanda::Cmdline;
use Amanda::Xfer qw( :constants );
use Amanda::Recovery::Planner;
use Amanda::Recovery::Clerk;
use Amanda::Recovery::Scan;

# Interactivity package
package Amanda::Interactivity::amfetchdump;
use POSIX qw( :errno_h );
use Amanda::MainLoop qw( :GIOCondition );
use vars qw( @ISA );
@ISA = qw( Amanda::Interactivity );

sub new {
    my $class = shift;

    my $self = {
	input_src => undef};
    return bless ($self, $class);
}

sub abort() {
    my $self = shift;

    if ($self->{'input_src'}) {
	$self->{'input_src'}->remove();
	$self->{'input_src'} = undef;
    }
}

sub user_request {
    my $self = shift;
    my %params = @_;
    my $buffer = "";

    my $message  = $params{'message'};
    my $label    = $params{'label'};
    my $err      = $params{'err'};
    my $chg_name = $params{'chg_name'};

    my $data_in = sub {
	my $b;
	my $n_read = POSIX::read(0, $b, 1);
	if (!defined $n_read) {
	    return if ($! == EINTR);
	    $self->abort();
	    return $params{'request_cb'}->(
		Amanda::Changer::Error->new('fatal',
			message => "Fail to read from stdin"));
	} elsif ($n_read == 0) {
	    $self->abort();
	    return $params{'request_cb'}->(
		Amanda::Changer::Error->new('fatal',
			message => "Aborted by user"));
	} else {
	    $buffer .= $b;
	    if ($b eq "\n") {
		my $line = $buffer;
		chomp $line;
		$buffer = "";
		$self->abort();
		return $params{'request_cb'}->(undef, $line);
	    }
	}
    };

    print STDERR "$err\n";
    print STDERR "Insert volume labeled '$label' in $chg_name\n";
    print STDERR "and press enter, or ^D to abort.\n";

    $self->{'input_src'} = Amanda::MainLoop::fd_source(0, $G_IO_IN|$G_IO_HUP|$G_IO_ERR);
    $self->{'input_src'}->set_callback($data_in);
    return;
};

package main;

sub usage {
    my ($msg) = @_;
    print STDERR <<EOF;
Usage: amfetchdump [-c|-C|-l] [-p|-n] [-a] [-O directory] [-d device]
    [-h] [--header-file file] [--header-fd fd] [-o configoption]* config
    hostname [diskname [datestamp [hostname [diskname [datestamp ... ]]]]]
EOF
    print STDERR "ERROR: $msg\n" if $msg;
    exit(1);
}

##
# main

Amanda::Util::setup_application("amfetchdump", "server", $CONTEXT_CMDLINE);

my $config_overrides = new_config_overrides($#ARGV+1);

my ($opt_config, $opt_no_reassembly, $opt_compress, $opt_compress_best, $opt_pipe,
    $opt_assume, $opt_leave, $opt_blocksize, $opt_device, $opt_chdir, $opt_header,
    $opt_header_file, $opt_header_fd, @opt_dumpspecs);
Getopt::Long::Configure(qw(bundling));
GetOptions(
    'version' => \&Amanda::Util::version_opt,
    'help|usage|?' => \&usage,
    'n' => \$opt_no_reassembly,
    'c' => \$opt_compress,
    'C' => \$opt_compress_best,
    'p' => \$opt_pipe,
    'a' => \$opt_assume,
    'l' => \$opt_leave,
    'h' => \$opt_header,
    'header-file=s' => \$opt_header_file,
    'header-fd=i' => \$opt_header_fd,
    'b=s' => \$opt_blocksize,
    'd=s' => \$opt_device,
    'O=s' => \$opt_chdir,
    'o=s' => sub { add_config_override_opt($config_overrides, $_[1]); },
) or usage();
usage() unless (@ARGV);
$opt_config = shift @ARGV;

$opt_compress = 1 if $opt_compress_best;

usage("must specify at least a hostname") unless @ARGV;
@opt_dumpspecs = Amanda::Cmdline::parse_dumpspecs([@ARGV],
    $Amanda::Cmdline::CMDLINE_PARSE_DATESTAMP | $Amanda::Cmdline::CMDLINE_PARSE_LEVEL);

usage("The -b option is no longer supported; set readblocksize in the tapetype section\n" .
      "of amanda.conf instead.")
    if ($opt_blocksize);
usage("-l is not compatible with -c or -C")
    if ($opt_leave and $opt_compress);
usage("-p is not compatible with -n")
    if ($opt_leave and $opt_no_reassembly);
usage("-h, --header-file, and --header-fd are mutually incompatible")
    if (($opt_header and $opt_header_file or $opt_header_fd)
	    or ($opt_header_file and $opt_header_fd));

set_config_overrides($config_overrides);
config_init($CONFIG_INIT_EXPLICIT_NAME, $opt_config);
my ($cfgerr_level, @cfgerr_errors) = config_errors();
if ($cfgerr_level >= $CFGERR_WARNINGS) {
    config_print_errors();
    if ($cfgerr_level >= $CFGERR_ERRORS) {
	die("errors processing config file");
    }
}

Amanda::Util::finish_setup($RUNNING_AS_DUMPUSER);

my $exit_status = 0;
my $clerk;
sub failure {
    my ($msg, $finished_cb) = @_;
    print STDERR "ERROR: $msg\n";
    $exit_status = 1;
    if ($clerk) {
	$clerk->quit(finished_cb => sub {
	    # ignore error
	    $finished_cb->();
	});
    } else {
	$finished_cb->();
    }
}

package main::Feedback;

use base 'Amanda::Recovery::Clerk::Feedback';
use Amanda::MainLoop;

sub new {
    my $class = shift;
    my ($chg, $dev_name) = @_;

    return bless {
	chg => $chg,
	dev_name => $dev_name,
    }, $class;
}

sub clerk_notif_part {
    my $self = shift;
    my ($label, $filenum, $header) = @_;

    print STDERR "amfetchdump: $filenum: restoring ", $header->summary(), "\n";
}

sub clerk_notif_holding {
    my $self = shift;
    my ($filename, $header) = @_;

    # this used to give the fd from which the holding file was being read.. why??
    print STDERR "Reading '$filename'\n", $header->summary(), "\n";
}

package main;

use Amanda::MainLoop qw( :GIOCondition );
sub main {
    my ($finished_cb) = @_;
    my $current_dump;
    my $plan;
    my @xfer_errs;
    my %all_filter;
    my $fetch_done;

    my $steps = define_steps
	cb_ref => \$finished_cb;

    step start => sub {
	my $chg;

	# first, go to opt_directory or the original working directory we
	# were started in
	my $destdir = $opt_chdir || Amanda::Util::get_original_cwd();
	if (!chdir($destdir)) {
	    return failure("Cannot chdir to $destdir: $!", $finished_cb);
	}

	my $interactivity = Amanda::Interactivity::amfetchdump->new();
	# if we have an explicit device, then the clerk doesn't get a changer --
	# we operate the changer via Amanda::Recovery::Scan
	if (defined $opt_device) {
	    $chg = Amanda::Changer->new($opt_device);
	    return failure($chg, $finished_cb) if $chg->isa("Amanda::Changer::Error");
	    my $scan = Amanda::Recovery::Scan->new(
				chg => $chg,
				interactivity => $interactivity);
	    return failure($scan, $finished_cb) if $scan->isa("Amanda::Changer::Error");
	    $clerk = Amanda::Recovery::Clerk->new(
		feedback => main::Feedback->new($chg, $opt_device),
		scan     => $scan);
	} else {
	    my $scan = Amanda::Recovery::Scan->new(
				interactivity => $interactivity);
	    return failure($scan, $finished_cb) if $scan->isa("Amanda::Changer::Error");

	    $clerk = Amanda::Recovery::Clerk->new(
		changer => $chg,
		feedback => main::Feedback->new($chg, undef),
		scan     => $scan);
	}

	# planner gets to plan against the same changer the user specified
	Amanda::Recovery::Planner::make_plan(
	    dumpspecs => [ @opt_dumpspecs ],
	    changer => $chg,
	    plan_cb => $steps->{'plan_cb'},
	    $opt_no_reassembly? (one_dump_per_part => 1) : ());
    };

    step plan_cb => sub {
	(my $err, $plan) = @_;
	return failure($err, $finished_cb) if $err;

	if (!@{$plan->{'dumps'}}) {
	    return failure("No matching dumps found", $finished_cb);
	}

	# if we are doing a -p operation, only keep the first dump
	if ($opt_pipe) {
	    @{$plan->{'dumps'}} = ($plan->{'dumps'}[0]);
	}

	my @needed_labels = $plan->get_volume_list();
	my @needed_holding = $plan->get_holding_file_list();
	if (@needed_labels) {
	    print STDERR (scalar @needed_labels), " volume(s) needed for restoration\n";
	    print STDERR "The following volumes are needed: ",
		join(" ", map { $_->{'label'} } @needed_labels ), "\n";
	}
	if (@needed_holding) {
	    print STDERR (scalar @needed_holding), " holding file(s) needed for restoration\n";
	    for my $hf (@needed_holding) {
		print "  $hf\n";
	    }
	}

	unless ($opt_assume) {
	    print STDERR "Press enter when ready\n";
	    my $resp = <STDIN>;
	}

	$steps->{'start_dump'}->();
    };

    step start_dump => sub {
	$current_dump = shift @{$plan->{'dumps'}};
	if (!$current_dump) {
	    return $steps->{'finished'}->();
	}

	$clerk->get_xfer_src(
	    dump => $current_dump,
	    xfer_src_cb => $steps->{'xfer_src_cb'});
    };

    step xfer_src_cb => sub {
	my ($errs, $hdr, $xfer_src, $directtcp_supported) = @_;
	return failure(join("; ", @$errs), $finished_cb) if $errs;

	# and set up the destination..
	my $dest_fh;
	if ($opt_pipe) {
	    $dest_fh = \*STDOUT;
	} else {
	    my $filename = sprintf("%s.%s.%s.%d",
		    $hdr->{'name'},
		    Amanda::Util::sanitise_filename("".$hdr->{'disk'}), # workaround SWIG bug
		    $hdr->{'datestamp'},
		    $hdr->{'dumplevel'});
	    if ($opt_no_reassembly) {
		$filename .= sprintf(".%07d", $hdr->{'partnum'});
	    }

	    # add an appropriate suffix
	    if ($opt_compress) {
		$filename .= ($hdr->{'compressed'} && $hdr->{'comp_suffix'})?
		    $hdr->{'comp_suffix'} : $Amanda::Constants::COMPRESS_SUFFIX;
	    }

	    if (!open($dest_fh, ">", $filename)) {
		return failure("Could not open '$filename' for writing: $!", $finished_cb);
	    }
	}

	my $xfer_dest = Amanda::Xfer::Dest::Fd->new($dest_fh);

	# set up any filters that need to be applied; decryption first
	my @filters;
	if ($hdr->{'encrypted'} and not $opt_leave) {
	    if ($hdr->{'srv_encrypt'}) {
		push @filters,
		    Amanda::Xfer::Filter::Process->new(
			[ $hdr->{'srv_encrypt'}, $hdr->{'srv_decrypt_opt'} ], 0);
	    } elsif ($hdr->{'clnt_encrypt'}) {
		push @filters,
		    Amanda::Xfer::Filter::Process->new(
			[ $hdr->{'clnt_encrypt'}, $hdr->{'clnt_decrypt_opt'} ], 0);
	    } else {
		return failure("could not decrypt encrypted dump: no program specified",
			    $finished_cb);
	    }

	    $hdr->{'encrypted'} = 0;
	    $hdr->{'srv_encrypt'} = '';
	    $hdr->{'srv_decrypt_opt'} = '';
	    $hdr->{'clnt_encrypt'} = '';
	    $hdr->{'clnt_decrypt_opt'} = '';
	    $hdr->{'encrypt_suffix'} = 'N';
	}

	if ($hdr->{'compressed'} and not $opt_compress and not $opt_leave) {
	    # need to uncompress this file

	    if ($hdr->{'srvcompprog'}) {
		# TODO: this assumes that srvcompprog takes "-d" to decrypt
		push @filters,
		    Amanda::Xfer::Filter::Process->new(
			[ $hdr->{'srvcompprog'}, "-d" ], 0);
	    } elsif ($hdr->{'clntcompprog'}) {
		# TODO: this assumes that clntcompprog takes "-d" to decrypt
		push @filters,
		    Amanda::Xfer::Filter::Process->new(
			[ $hdr->{'clntcompprog'}, "-d" ], 0);
	    } else {
		push @filters,
		    Amanda::Xfer::Filter::Process->new(
			[ $Amanda::Constants::UNCOMPRESS_PATH,
			  $Amanda::Constants::UNCOMPRESS_OPT ], 0);
	    }

	    # adjust the header
	    $hdr->{'compressed'} = 0;
	    $hdr->{'uncompress_cmd'} = '';
	} elsif (!$hdr->{'compressed'} and $opt_compress and not $opt_leave) {
	    # need to compress this file

	    my $compress_opt = $opt_compress_best?
		$Amanda::Constants::COMPRESS_BEST_OPT :
		$Amanda::Constants::COMPRESS_FAST_OPT;
	    push @filters,
		Amanda::Xfer::Filter::Process->new(
		    [ $Amanda::Constants::COMPRESS_PATH,
		      $compress_opt ], 0);

	    # adjust the header
	    $hdr->{'compressed'} = 1;
	    $hdr->{'uncompress_cmd'} = " $Amanda::Constants::UNCOMPRESS_PATH " .
		"$Amanda::Constants::UNCOMPRESS_OPT |";
	    $hdr->{'comp_suffix'} = $Amanda::Constants::COMPRESS_SUFFIX;
	}

	# write the header to the destination if requested
	$hdr->{'blocksize'} = Amanda::Holding::DISK_BLOCK_BYTES;
	if (defined $opt_header or defined $opt_header_file or defined $opt_header_fd) {
	    my $hdr_fh = $dest_fh;
	    if (defined $opt_header_file) {
		open($hdr_fh, ">", $opt_header_file)
		    or return failure("could not open '$opt_header_file': $!", $finished_cb);
	    } elsif (defined $opt_header_fd) {
		open($hdr_fh, "<&".($opt_header_fd+0))
		    or return failure("could not open fd $opt_header_fd: $!", $finished_cb);
	    }
	    syswrite $hdr_fh, $hdr->to_string(32768, 32768), 32768;
	}

	# start reading all filter stderr
	foreach my $filter (@filters) {
	    my $fd = $filter->get_stderr_fd();
	    $fd.="";
	    $fd = int($fd);
	    my $src = Amanda::MainLoop::fd_source($fd,
						 $G_IO_IN|$G_IO_HUP|$G_IO_ERR);
	    my $buffer = "";
	    $all_filter{$src} = 1;
	    $src->set_callback( sub {
		my $b;
		my $n_read = POSIX::read($fd, $b, 1);
		if (!defined $n_read) {
		    return;
		} elsif ($n_read == 0) {
		    delete $all_filter{$src};
		    $src->remove();
		    POSIX::close($fd);
		    if (!%all_filter and $fetch_done) {
			$finished_cb->();
		    }
		} else {
		    $buffer .= $b;
		    if ($b eq "\n") {
			my $line = $buffer;
			print STDERR "filter stderr: $line";
			chomp $line;
			debug("filter stderr: $line");
			$buffer = "";
		    }
		}
	    });
	}

	my $xfer = Amanda::Xfer->new([ $xfer_src, @filters, $xfer_dest ]);
	$xfer->start($steps->{'handle_xmsg'}, 0, $current_dump->{'bytes'});
	$clerk->start_recovery(
	    xfer => $xfer,
	    recovery_cb => $steps->{'recovery_cb'});
    };

    step handle_xmsg => sub {
	my ($src, $msg, $xfer) = @_;

	$clerk->handle_xmsg($src, $msg, $xfer);
	if ($msg->{'type'} == $XMSG_INFO) {
	    Amanda::Debug::info($msg->{'message'});
	} elsif ($msg->{'type'} == $XMSG_ERROR) {
	    push @xfer_errs, $msg->{'message'};
	}
    };

    step recovery_cb => sub {
	my %params = @_;

	@xfer_errs = (@xfer_errs, @{$params{'errors'}})
	    if $params{'errors'};
	return failure(join("; ", @xfer_errs), $finished_cb)
	    if @xfer_errs;
	return failure("recovery failed", $finished_cb)
	    if $params{'result'} ne 'DONE';

	$steps->{'start_dump'}->();
    };

    step finished => sub {
	if ($clerk) {
	    $clerk->quit(finished_cb => $steps->{'quit'});
	} else {
	    $steps->{'quit'}->();
	}
    };

    step quit => sub {
	my ($err) = @_;

	return failure($err, $finished_cb) if $err;

#do all filter are done reading stderr
	$fetch_done = 1;
        if (!%all_filter) {
	    $finished_cb->();
	}
    };
}

main(\&Amanda::MainLoop::quit);
Amanda::MainLoop::run();
Amanda::Util::finish_application();
exit $exit_status;
