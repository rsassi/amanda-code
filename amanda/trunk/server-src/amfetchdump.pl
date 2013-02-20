#! @PERL@
# Copyright (c) 2009-2012 Zmanda, Inc.  All Rights Reserved.
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

use lib '@amperldir@';
use strict;
use warnings;

use Getopt::Long;
use File::Basename;
use XML::Simple;
use IPC::Open3;

use Amanda::Device qw( :constants );
use Amanda::Debug qw( :logging );
use Amanda::Config qw( :init :getconf config_dir_relative );
use Amanda::Util qw( :constants :quoting );
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
use Amanda::Extract;

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
    [-h|--header-file file|--header-fd fd]
    [-decrypt|--no-decrypt|--server-decrypt|--client-decrypt]
    [--decompress|--no-decompress|--server-decompress|--client-decompress]
    [--extract --directory directory [--data-path (amanda|directtcp)]
    [--application-property='NAME=VALUE']*]
    [--init] [--restore]
    [-o configoption]* [--exact-match] config
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
    $opt_header_file, $opt_header_fd, @opt_dumpspecs,
    $opt_decrypt, $opt_server_decrypt, $opt_client_decrypt,
    $opt_decompress, $opt_server_decompress, $opt_client_decompress,
    $opt_init, $opt_restore,
    $opt_extract, $opt_directory, $opt_data_path, %application_property,
    $opt_exact_match);

my $NEVER = 0;
my $ALWAYS = 1;
my $ONLY_SERVER = 2;
my $ONLY_CLIENT = 3;
my $decrypt;
my $decompress;

debug("Arguments: " . join(' ', @ARGV));
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
    'decrypt!' => \$opt_decrypt,
    'server-decrypt' => \$opt_server_decrypt,
    'client-decrypt' => \$opt_client_decrypt,
    'decompress!' => \$opt_decompress,
    'server-decompress' => \$opt_server_decompress,
    'client-decompress' => \$opt_client_decompress,
    'extract' => \$opt_extract,
    'directory=s' => \$opt_directory,
    'data-path=s' => \$opt_data_path,
    'application-property=s' => \%application_property,
    'exact-match' => \$opt_exact_match,
    'init' => \$opt_init,
    'restore!' => \$opt_restore,
    'b=s' => \$opt_blocksize,
    'd=s' => \$opt_device,
    'O=s' => \$opt_chdir,
    'o=s' => sub { add_config_override_opt($config_overrides, $_[1]); },
) or usage();
usage() unless (@ARGV);
$opt_config = shift @ARGV;

if (defined $opt_compress and defined $opt_compress_best) {
    print STDERR "Can't use -c and -C\n";
    usage();
}

usage("The -b option is no longer supported; set readblocksize in the tapetype section\n" .
      "of amanda.conf instead.")
    if ($opt_blocksize);
usage("-l is not compatible with -c or -C")
    if ($opt_leave and $opt_compress);
usage("-p is not compatible with -n")
    if ($opt_leave and $opt_no_reassembly);
usage("-h, --header-file, and --header-fd are mutually incompatible")
    if (($opt_header and ($opt_header_file or $opt_header_fd))
	    or ($opt_header_file and $opt_header_fd));

     $opt_data_path = lc($opt_data_path) if defined ($opt_data_path);
usage("--data_path must be 'amanda' or 'directtcp'")
    if (defined $opt_data_path and $opt_data_path ne 'directtcp' and $opt_data_path ne 'amanda');

if (defined $opt_leave) {
    if (defined $opt_decrypt and $opt_decrypt) {
	print STDERR "-l is incompatible with --decrypt\n";
	usage();
    }
    if (defined $opt_server_decrypt) {
	print STDERR "-l is incompatible with --server-decrypt\n";
	usage();
    }
    if (defined $opt_client_decrypt) {
	print STDERR "-l is incompatible with --client-decrypt\n";
	usage();
    }
    if (defined $opt_decompress and $opt_decompress) {
	print STDERR "-l is incompatible with --decompress\n";
	usage();
    }
    if (defined $opt_server_decompress) {
	print STDERR "-l is incompatible with --server-decompress\n";
	usage();
    }
    if (defined $opt_client_decompress) {
	print STDERR "-l is incompatible with --client-decompress\n";
	usage();
    }
}

if (( defined $opt_directory and !defined $opt_extract) or
    (!defined $opt_directory and  defined $opt_extract)) {
    print STDERR "Both --directorty and --extract must be set\n";
    usage();
}
if (defined $opt_directory and defined $opt_extract) {
    $opt_decrypt = 1;
    if (defined $opt_server_decrypt or defined $opt_client_decrypt) {
	print STDERR "--server_decrypt or --client-decrypt is incompatible with --extract\n";
	usage();
    }
    $opt_decompress = 1;
    if (defined $opt_server_decompress || defined $opt_client_decompress) {
	print STDERR "--server-decompress r --client-decompress is incompatible with --extract\n";
	usage();
    }
    if (defined($opt_leave) +
	defined($opt_compress) +
	defined($opt_compress_best)) {
	print STDERR "Can't use -l -c or -C with --extract\n";
	usage();
    }
    if (defined $opt_pipe) {
	print STDERR "--pipe is incompatible with --extract\n";
	usage();
    }
    if (defined $opt_header) {
	print STDERR "--header is incompatible with --extract\n";
	usage();
    }
}

if (defined($opt_decrypt) +
    defined($opt_server_decrypt) +
    defined($opt_client_decrypt) > 1) {
    print STDERR "Can't use only on of --decrypt, --no-decrypt, --server-decrypt or --client-decrypt\n";
    usage();
}
if (defined($opt_decompress) +
    defined($opt_server_decompress) +
    defined($opt_client_decompress) > 1) {
    print STDERR "Can't use only on of --decompress, --no-decompress, --server-decompress or --client-decompress\n";
    usage();
}

if (defined($opt_compress) and
    defined($opt_decompress) +
    defined($opt_server_decompress) +
    defined($opt_client_decompress) > 0) {
    print STDERR "Can't specify -c with one of --decompress, --no-decompress, --server-decompress or --client-decompress\n";
    usage();
}
if (defined($opt_compress_best) and
    defined($opt_decompress) +
    defined($opt_server_decompress) +
    defined($opt_client_decompress) > 0) {
    print STDERR "Can't specify -C with one of --decompress, --no-decompress, --server-decompress or --client-decompress\n";
    usage();
}

$decompress = $ALWAYS;
$decrypt = $ALWAYS;
$decrypt = $NEVER  if defined $opt_leave;
$decrypt = $NEVER  if defined $opt_decrypt and !$opt_decrypt;
$decrypt = $ALWAYS if defined $opt_decrypt and $opt_decrypt;
$decrypt = $ONLY_SERVER if defined $opt_server_decrypt;
$decrypt = $ONLY_CLIENT if defined $opt_client_decrypt;

$opt_compress = 1 if $opt_compress_best;

$decompress = $NEVER  if defined $opt_compress;
$decompress = $NEVER  if defined $opt_leave;
$decompress = $NEVER  if defined $opt_decompress and !$opt_decompress;
$decompress = $ALWAYS if defined $opt_decompress and $opt_decompress;
$decompress = $ONLY_SERVER if defined $opt_server_decompress;
$decompress = $ONLY_CLIENT if defined $opt_client_decompress;

usage("must specify at least a hostname") unless @ARGV;
my $cmd_flags = $Amanda::Cmdline::CMDLINE_PARSE_DATESTAMP |
		$Amanda::Cmdline::CMDLINE_PARSE_LEVEL;
$cmd_flags |= $Amanda::Cmdline::CMDLINE_EXACT_MATCH if $opt_exact_match;
@opt_dumpspecs = Amanda::Cmdline::parse_dumpspecs([@ARGV], $cmd_flags);

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
use Data::Dumper;
sub failure {
    my ($msg, $finished_cb) = @_;
    print STDERR "ERROR: $msg\n";
    debug("FAILURE: $msg");
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
    my ($chg, $dev_name, $is_tty) = @_;

    return bless {
	chg => $chg,
	dev_name => $dev_name,
	is_tty => $is_tty,
    }, $class;
}

sub clerk_notif_part {
    my $self = shift;
    my ($label, $filenum, $header) = @_;

    print STDERR "\n" if $self->{'is_tty'};
    print STDERR "amfetchdump: $filenum: restoring ", $header->summary(), "\n";
}

sub clerk_notif_holding {
    my $self = shift;
    my ($filename, $header) = @_;

    # this used to give the fd from which the holding file was being read.. why??
    print STDERR "\n" if $self->{'is_tty'};
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
    my $recovery_done;
    my %recovery_params;
    my $timer;
    my $is_tty;
    my $delay;
    my $directtcp = 0;
    my @directtcp_command;
    my @init_needed_labels;
    my $init_label;
    my $scan;
    my $hdr;
    my $source_crc;
    my $dest_crc;
    my $xfer_src;
    my $xfer_dest;
    my $client_filter;
    my $native_filter;
    my $restore_native_crc;
    my $restore_client_crc;
    my $dest_is_server;
    my $dest_is_client;
    my $dest_is_native;

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

	$is_tty = -t STDERR;
	if($is_tty) {
	    $delay = 1000; # 1 second
	} else {
	    $delay = 5000; # 5 seconds
	}

	my $interactivity = Amanda::Interactivity::amfetchdump->new();
	# if we have an explicit device, then the clerk doesn't get a changer --
	# we operate the changer via Amanda::Recovery::Scan
	if (defined $opt_device) {
	    $chg = Amanda::Changer->new($opt_device);
	    return failure($chg, $finished_cb) if $chg->isa("Amanda::Changer::Error");
	    $scan = Amanda::Recovery::Scan->new(
				chg => $chg,
				interactivity => $interactivity);
	    return failure($scan, $finished_cb) if $scan->isa("Amanda::Changer::Error");
	    $clerk = Amanda::Recovery::Clerk->new(
		feedback => main::Feedback->new($chg, $opt_device, $is_tty),
		scan     => $scan);
	} else {
	    $scan = Amanda::Recovery::Scan->new(
				interactivity => $interactivity);
	    return failure($scan, $finished_cb) if $scan->isa("Amanda::Changer::Error");

	    $clerk = Amanda::Recovery::Clerk->new(
		changer => $chg,
		feedback => main::Feedback->new($chg, undef, $is_tty),
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
	    print STDERR "WARNING: Fetch first dump only because of -p argument\n" if @{$plan->{'dumps'}} > 1;
	    @{$plan->{'dumps'}} = ($plan->{'dumps'}[0]);
	}
	if ($opt_init) {
	    return $steps->{'init_seek_file'}->();
	}
	$steps->{'list_volume'}->();
    };

    step init_seek_file => sub {

	@init_needed_labels = $plan->get_volume_list();
	$steps->{'loop_init_seek_file'}->();
    };

    step loop_init_seek_file => sub {
	my $Xinit_label = shift @init_needed_labels;
	$init_label = $Xinit_label->{'label'};
	if (!$init_label) {
	    return $steps->{'end_init_seek_file'}->();
	}
	$scan->find_volume(label  => $init_label,
			   res_cb => $steps->{'init_seek_file_done_load'},
			   set_current => 0);
    };

    step init_seek_file_done_load => sub {
	my ($err, $res) = @_;
        return failure($err, $finished_cb) if ($err);

	my $dev = $res->{'device'};
	if (!$dev->start($Amanda::Device::ACCESS_READ, undef, undef)) {
	    $err = $dev->error_or_status();
	}
	for my $dump (@{$plan->{'dumps'}}) {
	    for my $part (@{$dump->{'parts'}}) {
		next unless defined $part; # skip parts[0]
		next unless defined $part->{'label'}; # skip holding parts
		next if $part->{'label'} ne $init_label;
		$dev->init_seek_file($part->{'filenum'});
	    }
	}
	$res->release(finished_cb => $steps->{'loop_init_seek_file'});
    };

    step end_init_seek_file => sub {
	if (defined $opt_restore && $opt_restore == 0) {
	    return $steps->{'finished'}->();
	}
	$steps->{'list_volume'}->();
    };

    step list_volume => sub {
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

	$recovery_done = 0;
	%recovery_params = ();

	$clerk->get_xfer_src(
	    dump => $current_dump,
	    xfer_src_cb => $steps->{'xfer_src_cb'});
    };

    step xfer_src_cb => sub {
	(my $errs, $hdr, $xfer_src, my $directtcp_supported) = @_;
	return failure(join("; ", @$errs), $finished_cb) if $errs;

	my $dle_str = $hdr->{'dle_str'};
	my $p1 = XML::Simple->new();
	my $dle = $p1->XMLin($dle_str);

	# and set up the destination..
	my $dest_fh;
	my @filters;

	# Take the CRC from the log if they are not in the header
	if (!defined $hdr->{'native_crc'} ||
            $hdr->{'native_crc'} =~ /^00000000:/) {
            $hdr->{'native_crc'} = $current_dump->{'native_crc'};
	}
	if (!defined $hdr->{'client_crc'} ||
            $hdr->{'client_crc'} =~ /^00000000:/) {
            $hdr->{'client_crc'} = $current_dump->{'client_crc'};
	}
	if (!defined $hdr->{'server_crc'} ||
            $hdr->{'server_crc'} =~ /^00000000:/) {
            $hdr->{'server_crc'} = $current_dump->{'server_crc'};
	}

	if (defined $opt_data_path and $opt_data_path eq 'directtcp' and !$directtcp_supported) {
	    return failure("The device can't do directtcp", $finished_cb);
	}
	$directtcp_supported = 0 if defined $opt_data_path and $opt_data_path eq 'amanda';
	if ($opt_extract) {
	    my $program = uc(basename($hdr->{program}));
	    my @argv;
	    if ($program ne "APPLICATION") {
		$directtcp_supported = 0;
		my %validation_programs = (
			"STAR" => [ $Amanda::Constants::STAR, qw(-x -f -) ],
			"DUMP" => [ $Amanda::Constants::RESTORE, qw(xbf 2 -) ],
			"VDUMP" => [ $Amanda::Constants::VRESTORE, qw(xf -) ],
			"VXDUMP" => [ $Amanda::Constants::VXRESTORE, qw(xbf 2 -) ],
			"XFSDUMP" => [ $Amanda::Constants::XFSRESTORE, qw(-v silent) ],
			"TAR" => [ $Amanda::Constants::GNUTAR, qw(--ignore-zeros -xf -) ],
			"GTAR" => [ $Amanda::Constants::GNUTAR, qw(--ignore-zeros -xf -) ],
			"GNUTAR" => [ $Amanda::Constants::GNUTAR, qw(--ignore-zeros -xf -) ],
			"SMBCLIENT" => [ $Amanda::Constants::GNUTAR, qw(-xf -) ],
			"PKZIP" => undef,
		);
		if (!exists $validation_programs{$program}) {
		    return failure("Unknown program '$program' in header; no validation to perform",
				   $finished_cb);
		}
		@argv = $validation_programs{$program};
	    } else {
		if (!defined $hdr->{application}) {
		    return failure("Application not set", $finished_cb);
		}
		my $program_path = $Amanda::Paths::APPLICATION_DIR . "/" .
				   $hdr->{application};
		if (!-x $program_path) {
		    return failure("Application '" . $hdr->{application} .
				   "($program_path)' not available on the server",
				   $finished_cb);
		}
		my %bsu_argv;
		$bsu_argv{'application'} = $hdr->{application};
		$bsu_argv{'config'} = $opt_config;
		$bsu_argv{'host'} = $hdr->{'name'};
		$bsu_argv{'disk'} = $hdr->{'disk'};
		$bsu_argv{'device'} = $dle->{'diskdevice'} if defined $dle->{'diskdevice'};
		my ($bsu, $err) = Amanda::Extract::BSU(%bsu_argv);
		if (defined $opt_data_path and $opt_data_path eq 'directtcp' and
		    !$bsu->{'data-path-directtcp'}) {
		    return failure("The application can't do directtcp", $finished_cb);
		}
		if ($directtcp_supported and !$bsu->{'data-path-directtcp'}) {
		    # application do not support directtcp
		    $directtcp_supported = 0;
		}

		push @argv, $program_path, "restore";
		push @argv, "--config", $opt_config;
		push @argv, "--host", $hdr->{'name'};
		push @argv, "--disk", $hdr->{'disk'};
		push @argv, "--device", $dle->{'diskdevice'} if defined ($dle->{'diskdevice'});
		push @argv, "--level", $hdr->{'dumplevel'};
		push @argv, "--directory", $opt_directory;

		if ($bsu->{'recover-dump-state-file'}) {
		    my $host = sanitise_filename("".$hdr->{'name'});
		    my $disk = sanitise_filename("".$hdr->{'disk'});
		    my $state_filename = getconf($CNF_INDEXDIR) . '/' . $host .
				 '/' . $disk . '/' . $hdr->{'datestamp'} . '_' .
				 $hdr->{'dumplevel'} . '.state';

		    if (-e $state_filename) {
			push @argv, "--recover-dump-state-file",
				    $state_filename;
		    }
		}

		# add application_property
		while (my($name, $value) = each(%application_property)) {
		    push @argv, "--".$name, $value if $value;
		}

		#merge property from header;
		while (my($name, $value) = each (%{$dle->{'backup-program'}->{'property'}})) {
		    if (!exists $application_property{$name}) {
			push @argv, "--".$name, $value->{'value'};
		    }
		}

	    }
	    $directtcp = $directtcp_supported;
	    if ($directtcp_supported) {
		$xfer_dest = Amanda::Xfer::Dest::DirectTCPListen->new();
		@directtcp_command = @argv;
	    } else {
		# set up the extraction command as a filter element, since
		# we need its stderr.
		debug("Running: ". join(' ',@argv));
		push @filters, Amanda::Xfer::Filter::Process->new(\@argv, 0);

		$dest_fh = \*STDOUT;
		$xfer_dest = Amanda::Xfer::Dest::Fd->new($dest_fh);
	    }
	} elsif ($opt_pipe) {
	    $dest_fh = \*STDOUT;
	    $xfer_dest = Amanda::Xfer::Dest::Fd->new($dest_fh);
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
	    $xfer_dest = Amanda::Xfer::Dest::Fd->new($dest_fh);
	}

	$timer = Amanda::MainLoop::timeout_source($delay);
	$timer->set_callback(sub {
	    my $size = $xfer_src->get_bytes_read();
	    if ($is_tty) {
		print STDERR "\r" . int($size/1024) . " kb ";
	    } else {
		print STDERR "READ SIZE: " . int($size/1024) . " kb\n";
	    }
	});

	$dest_is_server = 1;
	$dest_is_client = 0;
	$dest_is_native = 0;
	# set up any filters that need to be applied; decryption first
	if ($hdr->{'encrypted'} and
	    (($hdr->{'srv_encrypt'} and ($decrypt == $ALWAYS || $decrypt == $ONLY_SERVER)) ||
	     ($hdr->{'clnt_encrypt'} and ($decrypt == $ALWAYS || $decrypt == $ONLY_CLIENT)))) {
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

	    if (!$hdr->{'compressed'}) {
		$native_filter = Amanda::Xfer::Filter::Crc->new();
		push @filters, $native_filter;
		$dest_is_native = 1;
	    } elsif ($hdr->{'srv_encrypt'} and
	        (!$hdr->{'srvcompprog'} and
		 $dle->{'compress'} eq "SERVER-FAST" and
		 $dle->{'compress'} eq "SERVER-BEST")) {
		$client_filter = Amanda::Xfer::Filter::Crc->new();
		push @filters, $client_filter;
		$dest_is_client = 1;
	    }
	    $dest_is_server = 0;
	}

	if ($hdr->{'compressed'} and not $opt_compress and
	    (($hdr->{'srvcompprog'} and ($decompress == $ALWAYS || $decompress == $ONLY_SERVER)) ||
	     ($hdr->{'clntcompprog'} and ($decompress == $ALWAYS || $decompress == $ONLY_CLIENT)) ||
	     ($dle->{'compress'} and $dle->{'compress'} eq "SERVER-FAST" and ($decompress == $ALWAYS || $decompress == $ONLY_SERVER)) ||
	     ($dle->{'compress'} and $dle->{'compress'} eq "SERVER-BEST" and ($decompress == $ALWAYS || $decompress == $ONLY_SERVER)) ||
	     ($dle->{'compress'} and $dle->{'compress'} eq "FAST" and ($decompress == $ALWAYS || $decompress == $ONLY_CLIENT)) ||
	     ($dle->{'compress'} and $dle->{'compress'} eq "BEST" and ($decompress == $ALWAYS || $decompress == $ONLY_CLIENT)))) {
	    # need to uncompress this file
	    if ($hdr->{'encrypted'}) {
		print "Not decompressing because the backup image is not decrypted\n";
	    } elsif ($hdr->{'srvcompprog'}) {
		# TODO: this assumes that srvcompprog takes "-d" to decompress
		push @filters,
		    Amanda::Xfer::Filter::Process->new(
			[ $hdr->{'srvcompprog'}, "-d" ], 0);
	    } elsif ($hdr->{'clntcompprog'}) {
		# TODO: this assumes that clntcompprog takes "-d" to decompress
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

	    $native_filter = Amanda::Xfer::Filter::Crc->new();
	    push @filters, $native_filter;
	    $dest_is_native = 1;
	    $dest_is_client = 0;
	    $dest_is_server = 0;
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
	    $dest_is_server = 0;
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
	    next if !$filter->can('get_stderr_fd');
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
		    if (!%all_filter and $recovery_done) {
			$steps->{'filter_done'}->();
		    }
		} else {
		    $buffer .= $b;
		    if ($b eq "\n") {
			my $line = $buffer;
			chomp $line;
			if (length($line) > 1) {
			    print STDERR "filter stderr: $line\n";
			    debug("filter stderr: $line");
			}
			$buffer = "";
		    }
		}
	    });
	}

	my $xfer;
	if (@filters) {
	    $xfer = Amanda::Xfer->new([ $xfer_src, @filters, $xfer_dest ]);
	} else {
	    $xfer = Amanda::Xfer->new([ $xfer_src, $xfer_dest ]);
	}
	$xfer->start($steps->{'handle_xmsg'}, 0, $current_dump->{'bytes'});
	$clerk->start_recovery(
	    xfer => $xfer,
	    recovery_cb => $steps->{'recovery_cb'});
	if ($directtcp) {
	    my $addr = $xfer_dest->get_addrs();
	    push @directtcp_command, "--data-path", "DIRECTTCP";
	    push @directtcp_command, "--direct-tcp", "$addr->[0]->[0]:$addr->[0]->[1]";
	    debug("Running: ". join(' ', @directtcp_command));

	    my ($wtr, $rdr);
	    my $err = Symbol::gensym;
	    my $amndmp_pid = open3($wtr, $rdr, $err, @directtcp_command);
	    $amndmp_pid = $amndmp_pid;
	    my $file_to_close = 2;
	    my $amndmp_stdout_src = Amanda::MainLoop::fd_source($rdr,
						$G_IO_IN|$G_IO_HUP|$G_IO_ERR);
	    my $amndmp_stderr_src = Amanda::MainLoop::fd_source($err,
						$G_IO_IN|$G_IO_HUP|$G_IO_ERR);

	    $amndmp_stdout_src->set_callback( sub {
		my $line = <$rdr>;
		if (!defined $line) {
		    $file_to_close--;
		    $amndmp_stdout_src->remove();
		    if ($file_to_close == 0) {
			#abort the xfer
			$xfer->cancel() if $xfer->get_status != $XFER_DONE;
		    }
		    return;
		}
		chomp $line;
		debug("amndmp stdout: $line");
		print "$line\n";
	    });
	    $amndmp_stderr_src->set_callback( sub {
		my $line = <$err>;
		if (!defined $line) {
                    $file_to_close--;
                    $amndmp_stderr_src->remove();
                    if ($file_to_close == 0) {
			#abort the xfer
			$xfer->cancel() if $xfer->get_status != $XFER_DONE;
                    }
                    return;
		}
		chomp $line;
		debug("amndmp stderr: $line");
		print STDERR "$line\n";
	    });
	}
    };

    step handle_xmsg => sub {
	my ($src, $msg, $xfer) = @_;

	if ($msg->{'type'} == $XMSG_CRC) {
	    if ($msg->{'elt'} == $xfer_src) {
		$source_crc = $msg->{'crc'}.":".$msg->{'size'};
		debug("source_crc: $source_crc");
	    } elsif ($msg->{'elt'} == $xfer_dest) {
		$dest_crc = $msg->{'crc'}.":".$msg->{'size'};
		debug("dest_crc: $dest_crc");
	    } elsif (defined $native_filter and $msg->{'elt'} == $native_filter) {
		$restore_native_crc =  $msg->{'crc'}.":".$msg->{'size'};
		debug("restore_native_crc: $restore_native_crc");
	    } elsif (defined $client_filter and $msg->{'elt'} == $client_filter) {
		$restore_client_crc =  $msg->{'crc'}.":".$msg->{'size'};
		debug("restore_client_crc: $restore_client_crc");
	    } else {
		debug("unhandled XMSG_CRC $msg->{'elt'}");
	    }
	} else {
	    $clerk->handle_xmsg($src, $msg, $xfer);
	}
	if ($msg->{'type'} == $XMSG_INFO) {
	    Amanda::Debug::info($msg->{'message'});
	} elsif ($msg->{'type'} == $XMSG_ERROR) {
	    push @xfer_errs, $msg->{'message'};
	}
    };

    step recovery_cb => sub {
	%recovery_params = @_;
	$recovery_done = 1;

	$steps->{'filter_done'}->() if !%all_filter;
    };

    step filter_done => sub {
	if ($is_tty) {
	    print STDERR "\r" . int($recovery_params{'bytes_read'}/1024) . " kb ";
	} else {
	    print STDERR "READ SIZE: " . int($recovery_params{'bytes_read'}/1024) . " kb\n";
	}
	@xfer_errs = (@xfer_errs, @{$recovery_params{'errors'}})
	    if $recovery_params{'errors'};

	return failure(join("; ", @xfer_errs), $finished_cb)
	    if @xfer_errs;
	return failure("recovery failed", $finished_cb)
	    if $recovery_params{'result'} ne 'DONE';

	if (!$opt_no_reassembly) {
	    my $msg;
	    if (defined $hdr->{'native_crc'} and $hdr->{'native_crc'} !~ /^00000000:/ and
		defined $current_dump->{'native_crc'} and $current_dump->{'native_crc'} !~ /^00000000:/ and
		$hdr->{'native_crc'} ne $current_dump->{'native_crc'}) {
		$msg = "recovery failed: native-crc in header ($hdr->{'native_crc'}) and native-crc in log ($current_dump->{'native_crc'}) differ";
		print STDERR "$msg\n";
		debug($msg);
	    }
	    if (defined $hdr->{'client_crc'} and $hdr->{'client_crc'} !~ /^00000000:/ and
		defined $current_dump->{'client_crc'} and $current_dump->{'client_crc'} !~ /^00000000:/ and
		$hdr->{'client_crc'} ne $current_dump->{'client_crc'}) {
		$msg = "recovery failed: client-crc in header ($hdr->{'client_crc'}) and client-crc in log ($current_dump->{'client_crc'}) differ";
		print STDERR "$msg\n";
		debug($msg);
	    }

	    my $hdr_server_crc_size;
	    my $current_dump_server_crc_size;
	    my $source_crc_size;

	    if (defined $hdr->{'server_crc'}) {
		$hdr->{'server_crc'} =~ /[^:]*:(.*)/;
		$hdr_server_crc_size = $1;
	    }
	    if (defined $current_dump->{'server_crc'}) {
		$current_dump->{'server_crc'} =~ /[^:]*:(.*)/;
		$current_dump_server_crc_size = $1;
	    }
	    if (defined $source_crc) {
		$source_crc =~ /[^:]*:(.*)/;
		$source_crc_size = $1;
	    }

	    if (defined $hdr->{'server_crc'} and $hdr->{'server_crc'} !~ /^00000000:/ and
		defined $current_dump->{'server_crc'} and $current_dump->{'server_crc'} !~ /^00000000:/ and
		$hdr_server_crc_size == $current_dump_server_crc_size and
		$hdr->{'server_crc'} ne $current_dump->{'server_crc'}) {
		$msg = "recovery failed: server-crc in header ($hdr->{'server_crc'}) and server-crc in log ($current_dump->{'server_crc'}) differ";
		print STDERR "$msg\n";
		debug($msg);
	    }

	    if (defined $current_dump->{'server_crc'} and $current_dump->{'server_crc'} !~ /^00000000:/ and
		$current_dump_server_crc_size == $source_crc_size and
		$current_dump->{'server_crc'} ne $source_crc) {
		$msg = "recovery failed: server-crc ($current_dump->{'server_crc'}) and source_crc ($source_crc) differ",
		print STDERR "$msg\n";
		debug($msg);
	    }

	    if (defined $current_dump->{'native_crc'} and $current_dump->{'native_crc'} !~ /^00000000:/ and
		defined $restore_native_crc and $current_dump->{'native_crc'} ne $restore_native_crc) {
		$msg = "recovery failed: native-crc ($current_dump->{'native_crc'}) and restore-native-crc ($restore_native_crc) differ";
		print STDERR "$msg\n";
		debug($msg);
	    }
	    if (defined $current_dump->{'client_crc'} and $current_dump->{'client_crc'} !~ /^00000000:/ and
		defined $restore_client_crc and $current_dump->{'client_crc'} ne $restore_client_crc) {
		$msg = "recovery failed: client-crc ($current_dump->{'client_crc'}) and restore-client-crc ($restore_client_crc) differ";
		print STDERR "$msg\n";
		debug($msg);
	    }

	    debug("dest_is_native $dest_crc $restore_native_crc") if $dest_is_native;
	    debug("dest_is_client $dest_crc $restore_client_crc") if $dest_is_client;
	    debug("dest_is_server $dest_crc $source_crc") if $dest_is_server;
	    if ($dest_is_native and $restore_native_crc ne $dest_crc) {
		$msg = "recovery failed: dest-crc ($dest_crc) and restore-native-crc ($restore_native_crc) differ";
		print STDERR "$msg\n";
		debug($msg);
	    }
	    if ($dest_is_client and $restore_client_crc ne $dest_crc) {
		$msg = "recovery failed: dest-crc ($dest_crc) and restore-client-crc ($restore_client_crc) differ";
		print STDERR "$msg\n";
		debug($msg);
	    }
	    if ($dest_is_server and $source_crc ne $dest_crc) {
		$msg = "recovery failed: dest-crc ($dest_crc) and source-crc ($source_crc) differ";
		print STDERR "$msg\n";
		debug($msg);
	    }
	}
	$hdr = undef;
	$xfer_src = undef;
	$xfer_dest = undef;
	$native_filter = undef;
	$client_filter = undef;
	$source_crc = undef;
	$dest_crc = undef;
	$restore_native_crc = undef;
	$restore_client_crc = undef;

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

	if (defined $timer) {
	    $timer->remove();
	    $timer = undef;
	}
	print STDERR "\n" if $is_tty;
	return failure($err, $finished_cb) if $err;

	$finished_cb->();
    };
}

main(\&Amanda::MainLoop::quit);
Amanda::MainLoop::run();
Amanda::Util::finish_application();
exit $exit_status;
