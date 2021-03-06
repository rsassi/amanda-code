#!@PERL@
# Copyright (c) 2010 Zmanda Inc.  All Rights Reserved.
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
# Sunnyvale, CA 94086, USA, or: http://www.zmanda.com

use lib '@amperldir@';
use strict;
use warnings;

use Amanda::Config qw( :init :getconf );
use Amanda::Disklist;
use Amanda::DB::Catalog;
use FileHandle;
use Getopt::Long;
use Carp;
use POSIX;

sub Usage {
    print STDERR <<END;
Usage: $0 [[-config] CONFIG] [-hostwidth width] [-diskwidth width] [-skipmissed] [-last] [-num0] [-togo0] [-verbose]

This script generates to standard output an overview of the filesystems
dumped over time and the type of dump done on a particular day, such as
a full dump, or an incremental, or if the dump failed.

You may override the default configuration `@DEFAULT_CONFIG@' by using
the -config command line option.  On larger installations, this script
will take a while to run.  In this case, run it with --verbose to see
how far along it is.
END
    exit 1;
}

# overrideable defaults
my $opt_config		= undef;
my $opt_hostwidth	= 8;
my $opt_diskwidth	= 20;
my $opt_skipmissed	= 0;
my $opt_last		= 0;
my $opt_num0		= 0;
my $opt_togo0		= 0;
my $opt_verbose		= 0;

GetOptions('config=s'		=> \$opt_config,
	   'hostwidth=i'	=> \$opt_hostwidth,
	   'diskwidth=i'	=> \$opt_diskwidth,
	   'skipmissed'		=> \$opt_skipmissed,
	   'last'		=> \$opt_last,
	   'num0'		=> \$opt_num0,
	   'togo0'		=> \$opt_togo0,
	   'verbose'		=> \$opt_verbose)
or Usage();

if(@ARGV == 1) {
  $opt_config = $ARGV[0];
} else {
  Usage();
}

#Initialize configuration
config_init($CONFIG_INIT_EXPLICIT_NAME, $opt_config);
my ($cfgerr_level, @cfgerr_errors) = config_errors();
if ($cfgerr_level >= $CFGERR_WARNINGS) {
    config_print_errors();
    if ($cfgerr_level >= $CFGERR_ERRORS) {
        die("errors processing config file");
    }
}

# read disklist
$cfgerr_level = Amanda::Disklist::read_disklist();
die("Config errors") if ($cfgerr_level >= $CFGERR_WARNINGS);

my %disks = ();
foreach my $dle (Amanda::Disklist::all_disks()) {
    $disks{$dle->{"host"}->{"hostname"}}{$dle->{"name"}}++;
}

# Get dumps
my %dates = ();
my %level = ();
my ($date, $host, $disk);
$opt_verbose and
    print STDERR "Processing $opt_config dumps\n";
foreach my $dump (Amanda::DB::Catalog::sort_dumps(['hostname','diskname','write_timestamp'],Amanda::DB::Catalog::get_dumps())) {
    $host = $dump->{"hostname"};
    $disk = $dump->{"diskname"};
    $date = substr($dump->{"dump_timestamp"},0,8);

    if (defined $disks{$host}{$disk}) {
        defined($level{$host}{$disk}{$date}) or
            $level{$host}{$disk}{$date} = '';
        $level{$host}{$disk}{$date} .= ($dump->{"status"} eq 'OK') ? $dump->{"level"} : 'E';
        $dates{$date}++;
    }
}

# Process the status to arrive at a "last" status
if ($opt_last) {
    for $host (sort keys %disks) {
        for $disk (sort keys %{$disks{$host}}) {
	    $level{$host}{$disk}{"0000LAST"} = '';
	    for $date (sort keys %dates) {
	        if ($level{$host}{$disk}{$date} eq "E"
		     && $level{$host}{$disk}{"0000LAST"} =~ /^\d/ ) {
		    $level{$host}{$disk}{"0000LAST"} .= $level{$host}{$disk}{$date};
	        } elsif ($level{$host}{$disk}{$date} eq "") {
		    $level{$host}{$disk}{"0000LAST"} =~ s/E//;
	        } else {
		    $level{$host}{$disk}{"0000LAST"} = $level{$host}{$disk}{$date};
	        }
	    }
        }
    }
}

# Number of level 0 backups
if ($opt_num0) {
    for $host (sort keys %disks) {
        for $disk (sort keys %{$disks{$host}}) {
            $level{$host}{$disk}{'0000NML0'} = 0;
            for $date (sort keys %dates) {
                if ($level{$host}{$disk}{$date} =~ /0/) {
                    $level{$host}{$disk}{'0000NML0'} += 1;
                }
            }
        }
    }
}

# Runs to the last level 0
if ($opt_togo0) {
    for $host (sort keys %disks) {
        for $disk (sort keys %{$disks{$host}}) {
            $level{$host}{$disk}{'0000TOGO'} = 0;
            my $togo=0;
            for $date (sort keys %dates) {
                if ($level{$host}{$disk}{$date} =~ /0/) {
                    $level{$host}{$disk}{'0000TOGO'} = $togo;
                }
                $togo++;
            }
        }
    }
}

unless ($opt_skipmissed)
# touch all the dates just in case whole days were missed.
{
    my ($start, $finish) = 
	map {
	    my($y,$m,$d) = /(....)(..)(..)/;
	    POSIX::mktime(0,0,0,$d,$m-1,$y-1900);
	} (sort keys %dates)[0,-1];

    # Special case of only one date
    return if !defined($finish);

    while ($start < $finish) {
	my @l = localtime $start;
	$dates{sprintf("%d%02d%02d", 1900+$l[5], $l[4]+1, $l[3])}++;
	$start += 86400;
    }
}

#Add the "last" entry    
$dates{"0000LAST"}=1 if ($opt_last);

#Add the "Number of Level 0s" entry
$dates{"0000NML0"}=1 if ($opt_num0);

#Add the "Runs to go" entry
$dates{"0000TOGO"}=1 if ($opt_togo0);

# make formats

my $top_format = "format TOP =\n\n" .
    sprintf("%-0${opt_hostwidth}s %-0${opt_diskwidth}s ", '', 'date') .
    join(' ', map((/....(..)../)[0], sort keys %dates)) . "\n" .
    sprintf("%-0${opt_hostwidth}s %-0${opt_diskwidth}s ", 'host', 'disk') .
    join(' ', map((/......(..)/)[0], sort keys %dates)) . "\n" .
    "\n.\n";

+ local ($::thishost,$::thisdisk);
my $out_format = "format STDOUT =\n" .
    "@" . "<" x ($opt_hostwidth - 1) . ' ' .
    "@" . "<" x ($opt_diskwidth - 1) . ' ' .
    '@> ' x scalar(keys %dates) . "\n" .
    join(', ', '$::thishost', '$::thisdisk',
       map("substr(\$level{\$::thishost}{\$::thisdisk}{'$_'},-2)", sort keys %dates)) . "\n" .
    ".\n";

eval $top_format;
die $@ if $@;
$^ = 'TOP';
eval $out_format;
die $@ if $@;

for $::thishost (sort keys %disks) {
    for $::thisdisk (sort keys %{$disks{$::thishost}}) {
	write;
    }
}
