#! @PERL@
use lib '@amperldir@';
use strict;

use File::Basename;
use Getopt::Long;

use Amanda::Device qw( :constants );
use Amanda::Debug qw( :logging );
use Amanda::Config qw( :init :getconf config_dir_relative );
use Amanda::Logfile;
use Amanda::Util qw( :running_as_flags );
use Amanda::Tapefile;
use Amanda::Changer;

# Have all images been verified successfully so far?
my $all_success = 1;

sub usage {
    print <<EOF;
USAGE:	amcheckdump config [ --timestamp|-t timestamp ] [-o configoption]*
    amcheckdump validates Amanda dump images by reading them from storage
volume(s), and verifying archive integrity if the proper tool is locally
available. amcheckdump does not actually compare the data located in the image
to anything; it just validates that the archive stream is valid.
    Arguments:
	config       - The Amanda configuration name to use.
	-t timestamp - The run of amdump or amflush to check. By default, check
			the most recent dump; if this parameter is specified,
			check the most recent dump matching the given
			date- or timestamp.
	-o configoption	- see the CONFIGURATION OVERRIDE section of amanda(8)
EOF
    exit(1);
}

# Find the most recent logfile name matching the given timestamp
sub find_logfile_name($) {
    my $timestamp = shift @_;
    my $rval;
    my $config_dir = config_dir_relative(getconf($CNF_LOGDIR));
    # First try log.$datestamp.$seq
    for (my $seq = 0;; $seq ++) {
        my $logfile = sprintf("%s/log.%s.%u", $config_dir, $timestamp, $seq);
        if (-f $logfile) {
            $rval = $logfile;
        } else {
            last;
        }
    }
    return $rval if defined $rval;

    # Next try log.$datestamp.amflush
    $rval = sprintf("%s/log.%s.amflush", $config_dir, $timestamp);

    return $rval if -f $rval;

    # Finally try log.datestamp.
    $rval = sprintf("%s/log.%s.amflush", $config_dir, $timestamp);
    
    return $rval if -f $rval;

    # No dice.
    return undef;
}

## Device management

my $changer_init_done = 0;
my $current_device;
my $current_device_label;

sub find_next_device {
    my $label = shift;
    if (getconf_seen($CNF_TPCHANGER)) {
	# We're using a changer script.
	if (!$changer_init_done) {
	    my $error = (Amanda::Changer::reset())[0];
	    critical($error) if $error;
	    $changer_init_done = 1;
	}
	my ($error, $slot, $tapedev) = Amanda::Changer::find($label);
	if ($error) {
	    critical("Error operating changer: $error.");
	} elsif ($slot eq "<none>") {
	    critical("Could not find tape label $label in changer.");
	} else {
	    return $tapedev;
	}
    } else {
	# The user is changing tapes for us.
	my $device_name = getconf($CNF_TAPEDEV);
	printf("Insert volume with label %s in device %s and press ENTER: ",
	       $label, $device_name);
	<>;
	return $device_name;
    }
}

# Try to open a device containing a volume with the given label.  Returns undef
# if there is a problem.
sub try_open_device {
    my ($label) = @_;

    # can we use the same device as last time?
    if ($current_device_label eq $label) {
	return $current_device;
    }

    # nope -- get rid of that device
    close_device();

    my $device_name = find_next_device($label);
    if ( !$device_name ) {
	print "Could not find a device for label '$label'.\n";
        return undef;
    }

    my $device = Amanda::Device->new($device_name);
    if ( !$device ) {
	print "Could not open '$device_name'.\n";
        return undef;
    }

    $device->set_startup_properties_from_config();

    my $label_status = $device->read_label();
    if ($label_status != $READ_LABEL_STATUS_SUCCESS) {
	print "Could not read device $device_name: one of ",
	     join(", ", ReadLabelStatusFlags_to_strings($label_status)),
	     "\n";
	return undef;
    }

    if ($device->{volume_label} ne $label) {
	printf("Labels do not match: Expected '%s', but the device contains '%s'.\n",
		     $label, $device->{volume_label});
	return undef;
    }

    if (!$device->start($ACCESS_READ, undef, undef)) {
	printf("Error reading device %s.\n", $device_name);
	return undef;
    }

    $current_device = $device;
    $current_device_label = $device->{volume_label};

    return $device;
}

sub close_device {
    $current_device = undef;
    $current_device_label = undef;
}

## Validation application

my ($current_validation_pid, $current_validation_pipeline, $current_validation_image);

# Return a filehandle for the validation application that will handle this
# image.  This function takes care of split dumps.  At the moment, we have
# a single "current" validation application, and as such assume that split dumps
# are stored contiguously and in order on the volume.
sub open_validation_app {
    my ($image, $header) = @_;

    # first, see if this is the same image we were looking at previously
    if (defined($current_validation_image)
	and $current_validation_image->{timestamp} eq $image->{timestamp}
	and $current_validation_image->{hostname} eq $image->{hostname}
	and $current_validation_image->{diskname} eq $image->{diskname}
	and $current_validation_image->{level} == $image->{level}) {
	# TODO: also check that the part number is correct
        print "Continuing with previously started validation process.\n";
	return $current_validation_pipeline;
    }

    # nope, new image.  close the previous pipeline
    close_validation_app();
	
    my $validation_command = find_validation_command($header);
    print "  using '$validation_command'.\n";
    $current_validation_pid = open($current_validation_pipeline, "|-", $validation_command);
        
    if (!$current_validation_pid) {
	print "Can't execute validation command: $!\n";
	undef $current_validation_pid;
	undef $current_validation_pipeline;
	return undef;
    }

    $current_validation_image = $image;
    return $current_validation_pipeline;
}

# Close any running validation app, checking its exit status for errors.  Sets
# $all_success to false if there is an error.
sub close_validation_app {
    if (!defined($current_validation_pipeline)) {
	return;
    }

    # first close the applications standard input to signal it to stop
    if (!close($current_validation_pipeline)) {
	my $exit_value = $? >> 8;
	print "Validation process returned $exit_value (full status $?)\n";
	$all_success = 0; # flag this as a failure
    }

    $current_validation_pid = undef;
    $current_validation_pipeline = undef;
    $current_validation_image = undef;
}

# Given a dumpfile_t, figure out the command line to validate.
sub find_validation_command {
    my ($header) = @_;

    # We base the actual archiver on our own table, but just trust
    # whatever is listed as the decrypt/uncompress commands.
    my $program = uc(basename($header->{program}));
    
    my $validation_program;
    my %validation_programs = (
        "DUMP" => "@RESTORE@ tbf 2 -",
        "VDUMP" => "@VRESTORE@ tf -",
        "VXDUMP" => "@VXRESTORE@ tbf 2 -",
        "XFSDUMP" => "@XFSRESTORE@ -t -v silent -",
        "TAR" => "@GNUTAR@ tf -",
        "GTAR" => "@GNUTAR@ tf -",
        "GNUTAR" => "@GNUTAR@ tf -",
        "SMBCLIENT" => "@SAMBA_CLIENT@ tf -",
    );

    $validation_program = $validation_programs{$program};
    if (!defined $validation_program) {
        warning("Could not determine validation for dumper $program; ".
             "Will send dumps to /dev/null instead.");
        $validation_program = "cat > /dev/null";
    } else {
        # This is to clean up any extra output the program doesn't read.
        $validation_program .= " > /dev/null && cat > /dev/null";
    }
    
    my $cmdline = "";
    if (defined $header->{decrypt_cmd} && 
        length($header->{decrypt_cmd}) > 0) {
        $cmdline .= $header->{decrypt_cmd};
    }
    if (defined $header->{uncompress_cmd} && 
        length($header->{uncompress_cmd}) > 0) {
        $cmdline .= $header->{uncompress_cmd};
    }
    $cmdline .= $validation_program;

    return $cmdline;
}

## Application initialization

Amanda::Util::setup_application("amcheckdump", "server", "cmdline");

my $timestamp = undef;
my $config_overwrites = new_config_overwrites($#ARGV+1);

Getopt::Long::Configure(qw(bundling));
GetOptions(
    'timestamp|t=s' => \$timestamp,
    'help|usage|?' => \&usage,
    'o=s' => sub { add_config_overwrite_opt($config_overwrites, $_[1]); },
) or usage();

usage() if (@ARGV < 1);

my $config_name = shift @ARGV;
if (!config_init($CONFIG_INIT_EXPLICIT_NAME |
                 $CONFIG_INIT_FATAL, $config_name)) {
    critical('errors processing config file "' . 
               Amanda::Config::get_config_filename() . '"');
}
apply_config_overwrites($config_overwrites);

Amanda::Util::finish_setup($RUNNING_AS_DUMPUSER);

# Read the tape list.
my $tl = Amanda::Tapefile::read_tapelist(config_dir_relative(getconf($CNF_TAPELIST)));

# If we weren't given a timestamp, find the newer of
# amdump.1 or amflush.1 and extract the datestamp from it.
if (!defined $timestamp) {
    my $amdump_log = config_dir_relative(getconf($CNF_LOGDIR)) . "/amdump.1";
    my $amflush_log = config_dir_relative(getconf($CNF_LOGDIR)) . "/amflush.1";
    my $logfile;
    if (-f $amflush_log && -f $amdump_log &&
         -M $amflush_log  < -M $amdump_log) {
         $logfile=$amflush_log;
    } elsif (-f $amdump_log) {
         $logfile=$amdump_log;
    } elsif (-f $amflush_log) {
         $logfile=$amflush_log;
    } else {
	print "Could not find any dump log file.\n";
	exit;
    }

    # extract the datestamp from the dump log
    open (AMDUMP, "<$logfile") || critical();
    while(<AMDUMP>) {
	if (/^amdump: starttime (\d*)$/) {
	    $timestamp = $1;
	}
	elsif (/^amflush: starttime (\d*)$/) {
	    $timestamp = $1;
	}
	elsif (/^planner: timestamp (\d*)$/) {
	    $timestamp = $1;
	}
    }
    close AMDUMP;
}

# Find all logfiles matching our timestamp
my @logfiles =
    grep { $_ =~ /^log\.$timestamp(?:\.[0-9]+|\.amflush)?$/ }
    Amanda::Logfile::find_log();

if (!@logfiles) {
    critical("Can't find any logfiles with timestamp $timestamp.");
}

# compile a list of *all* dumps in those logfiles
my $logfile_dir = config_dir_relative(getconf($CNF_LOGDIR));
my @images;
for my $logfile (@logfiles) {
    push @images, Amanda::Logfile::search_logfile(undef, $timestamp,
                                                  "$logfile_dir/$logfile", 1);
}

# filter only "ok" dumps, removing partial and failed dumps
@images = Amanda::Logfile::dumps_match([@images],
	undef, undef, undef, undef, 1);

if (!@images) {
    critical("Could not find any matching dumps");
}

# Find unique tapelist, using a hash to filter duplicate tapes
my %tapes = map { ($_->{label}, undef) } @images;
my @tapes = sort { $a cmp $b } keys %tapes;

if (!@tapes) {
    critical("Could not find any matching dumps");
}

printf("You will need the following tape%s: %s\n", (@tapes > 1) ? "s" : "",
       join(", ", @tapes));

# Now loop over the images, verifying each one.  

IMAGE:
for my $image (@images) {
    # Currently, L_PART results will be n/x, n >= 1, x >= -1
    # In the past (before split dumps), L_PART could be --
    # Headers can give partnum >= 0, where 0 means not split.
    my $logfile_part = 1; # assume this is not a split dump
    if ($image->{partnum} =~ m$(\d+)/(-?\d+)$) {
        $logfile_part = $1;
    }

    printf("Validating image %s:%s datestamp %s level %s part %s on tape %s file #%d\n",
           $image->{hostname}, $image->{diskname}, $image->{timestamp},
           $image->{level}, $logfile_part, $image->{label}, $image->{filenum});

    # note that if there is a device failure, we may try the same device
    # again for the next image.  That's OK -- it may give a user with an
    # intermittent drive some indication of such.
    my $device = try_open_device($image->{label});
    if (!defined $device) {
	# error message already printed
	$all_success = 0;
	next IMAGE;
    }

    # Now get the header from the device
    my $header = $device->seek_file($image->{filenum});
    if (!defined $header) {
        printf("Could not seek to file %d of volume %s.\n",
                     $image->{filenum}, $image->{label});
	$all_success = 0;
        next IMAGE;
    }

    # Make sure that the on-device header matches what the logfile
    # told us we'd find.

    my $volume_part = $header->{partnum};
    if ($volume_part == 0) {
        $volume_part = 1;
    }

    if ($image->{timestamp} ne $header->{datestamp} ||
        $image->{hostname} ne $header->{name} ||
        $image->{diskname} ne $header->{disk} ||
        $image->{level} != $header->{dumplevel} ||
        $logfile_part != $volume_part) {
        printf("Details of dump at file %d of volume %s do not match logfile.\n",
                     $image->{filenum}, $image->{label});
	$all_success = 0;
        next IMAGE;
    }
    
    # get the validation application pipeline that will process this dump.
    my $pipeline = open_validation_app($image, $header);

    # send the datastream from the device straight to the application
    if (!$device->read_to_fd(fileno($pipeline))) {
        print "Error reading device or writing data to validation command.\n";
	$all_success = 0;
	next IMAGE;
    }
}

# clean up
close_validation_app();
close_device();

exit($all_success? 0 : 1);
