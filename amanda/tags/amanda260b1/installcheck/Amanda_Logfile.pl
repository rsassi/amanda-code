# Copyright (c) 2006 Zmanda Inc.  All Rights Reserved.
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
# Contact information: Zmanda Inc, 505 N Mathlida Ave, Suite 120
# Sunnyvale, CA 94085, USA, or: http://www.zmanda.com

use Test::More qw(no_plan);
use File::Path;
use Amconfig;
use strict;

use lib "@amperldir@";
use Amanda::Paths;
use Amanda::Tapefile;
use Amanda::Logfile qw(:logtype_t :program_t open_logfile get_logline close_logfile);
use Amanda::Config qw( :init :getconf config_dir_relative );

# write a logfile and return the filename
sub write_logfile {
    my ($contents) = @_;
    my $filename = "$AMANDA_TMPDIR/Amanda_Logfile_test.log";

    if (!-e $AMANDA_TMPDIR) {
	mkpath($AMANDA_TMPDIR);
    }

    open my $logfile, ">", $filename or die("Could not create temporary log file");
    print $logfile $contents;
    close $logfile;

    return $filename;
}

####
## RAW LOGFILE ACCESS

my $logfile;
my $logdata;

##
# Test out the constant functions

is(logtype_t_to_string($L_MARKER), "L_MARKER", "logtype_t_to_string works");
is(program_t_to_string($P_DRIVER), "P_DRIVER", "program_t_to_string works");

##
# Test a simple logfile

$logdata = <<END;
START planner date 20071026183200
END

$logfile = open_logfile(write_logfile($logdata));
ok($logfile, "can open a simple logfile");
is_deeply([ get_logline($logfile) ], 
	  [ $L_START, $P_PLANNER, "date 20071026183200" ],
	  "reads START line correctly");
ok(!get_logline($logfile), "no second line");
close_logfile($logfile);

##
# Test continuation lines

$logdata = <<END;
INFO chunker line1
  line2
END

$logfile = open_logfile(write_logfile($logdata));
ok($logfile, "can open a logfile containing conitinuation lines");
is_deeply([ get_logline($logfile) ],
	  [ $L_INFO, $P_CHUNKER, "line1" ], 
	  "can read INFO line");
is_deeply([ get_logline($logfile) ],
	  [ $L_CONT, $P_CHUNKER, "line2" ], 
	  "can read continuation line");
ok(!get_logline($logfile), "no third line");
close_logfile($logfile);

##
# Test skipping blank lines

# (retain the two blank lines in the following:)
$logdata = <<END;

STATS taper foo

END

$logfile = open_logfile(write_logfile($logdata));
ok($logfile, "can open a logfile containing blank lines");
is_deeply([ get_logline($logfile) ], 
	  [ $L_STATS, $P_TAPER, "foo" ],
	  "reads non-blank line correctly");
ok(!get_logline($logfile), "no second line");
close_logfile($logfile);

##
# Test BOGUS values and short lines

$logdata = <<END;
SOMETHINGWEIRD somerandomprog bar
MARKER amflush
MARKER amflush put something in curstr
PART
END

$logfile = open_logfile(write_logfile($logdata));
ok($logfile, "can open a logfile containing bogus entries");
is_deeply([ get_logline($logfile) ], 
	  [ $L_BOGUS, $P_UNKNOWN, "bar" ],
	  "can read line with bogus program and logtype");
is_deeply([ get_logline($logfile) ], 
	  [ $L_MARKER, $P_AMFLUSH, "" ],
	  "can read line with an empty string");
ok(get_logline($logfile), "can read third line (to fill in curstr with some text)");
is_deeply([ get_logline($logfile) ], 
	  [ $L_PART, $P_UNKNOWN, "" ],
	  "can read a one-word line, with P_UNKNOWN");
ok(!get_logline($logfile), "no next line");
close_logfile($logfile);

## HIGHER-LEVEL FUNCTIONS

# a utility function for is_deeply checks, below.  Converts a hash to
# an array, for more succinct comparisons
sub res2arr {
    my ($res) = @_;
    return [
	$res->{'timestamp'},
	$res->{'hostname'},
	$res->{'diskname'},
	$res->{'level'},
	$res->{'label'},
	$res->{'filenum'},
	$res->{'status'},
	$res->{'partnum'}
    ];
}

# set up a basic config
my $testconf = Amconfig->new();
$testconf->add_param("tapecycle", "20");
$testconf->write();

# load the config
ok(config_init($CONFIG_INIT_EXPLICIT_NAME, "TESTCONF"), "config_init is OK");
my $tapelist = config_dir_relative("tapelist");

# set up and read the tapelist
open my $tlf, ">", $tapelist or die("Could not write tapelist");
print $tlf "20071111010002 TESTCONF004 reuse\n";
print $tlf "20071110010002 TESTCONF003 reuse\n";
print $tlf "20071109010002 TESTCONF002 reuse\n";
print $tlf "20071108010001 TESTCONF001 reuse\n";
close $tlf;
Amanda::Tapefile::read_tapelist($tapelist) == 0 or die("Could not read tapelist");

# set up a number of logfiles in logdir.
my $logf;
my $logdir = $testconf->{'logdir'};

# (an old log file that should be ignored)
open $logf, ">", "$logdir/log.20071106010002.0" or die("Could not write logfile");
print $logf "START taper datestamp 20071107010002 label TESTCONF017 tape 1\n";
close $logf;

# (a logfile with two tapes)
open $logf, ">", "$logdir/log.20071106010002.0" or die("Could not write logfile");
print $logf "START taper datestamp 20071106010002 label TESTCONF018 tape 1\n";
print $logf "START taper datestamp 20071106010002 label TESTCONF019 tape 2\n";
close $logf;

open $logf, ">", "$logdir/log.20071108010001.0" or die("Could not write logfile");
print $logf "START taper datestamp 20071108010001 label TESTCONF001 tape 1\n";
close $logf;

# a logfile with some detail, to run search_logfile against
open $logf, ">", "$logdir/log.20071109010002.0" or die("Could not write logfile");
print $logf <<EOF;
START taper datestamp 20071109010002 label TESTCONF002 tape 1
PART taper TESTCONF002 1 clihost /usr 20071109010002 1 0 [regular single part PART]
DONE taper clihost /usr 20071109010002 1 0 [regular single part DONE]
PART taper TESTCONF002 2 clihost "/my documents" 20071109010002 1 0 [diskname quoting]
DONE taper clihost "/my documents" 20071109010002 1 0 [diskname quoting]
PART taper TESTCONF002 3 thatbox /var 1 [regular 'old style' PART]
DONE taper thatbox /var 1 [regular 'old style' DONE]
PART taper TESTCONF002 4 clihost /home 20071109010002 1/5 0 [multi-part dump]
PART taper TESTCONF002 5 clihost /home 20071109010002 2/5 0 [multi-part dump]
PART taper TESTCONF002 6 clihost /home 20071109010002 3/5 0 [multi-part dump]
PART taper TESTCONF002 7 clihost /home 20071109010002 4/5 0 [multi-part dump]
PART taper TESTCONF002 8 clihost /home 20071109010002 5/5 0 [multi-part dump]
DONE taper clihost /home 20071109010002 5 0 [multi-part dump]
PART taper TESTCONF002 9 thatbox /u_lose 20071109010002 1/4 2 [multi-part failure]
PART taper TESTCONF002 10 thatbox /u_lose 20071109010002 2/4 2 [multi-part failure]
PART taper TESTCONF002 11 thatbox /u_lose 20071109010002 3/4 2 [multi-part failure]
FAIL taper thatbox /u_lose 20071109010002 2 "Oh no!"
DONE taper thatbox /u_lose 20071109010002 4 2 [multi-part failure]
EOF
close $logf;

# "old-style amflush log"
open $logf, ">", "$logdir/log.20071110010002.amflush" or die("Could not write logfile");
print $logf "START taper datestamp 20071110010002 label TESTCONF003 tape 1\n";
close $logf;

# "old-style main log"
open $logf, ">", "$logdir/log.20071111010002" or die("Could not write logfile");
print $logf "START taper datestamp 20071111010002 label TESTCONF004 tape 1\n";
close $logf;

is_deeply([ Amanda::Logfile::find_log() ],
	  [ "log.20071111010002", "log.20071110010002.amflush",
	    "log.20071109010002.0", "log.20071108010001.0" ],
	  "find_log returns correct logfiles in the correct order");

my @results;
my @results_arr;

@results = Amanda::Logfile::search_logfile("TESTCONF002", "20071109010002",
					   "$logdir/log.20071109010002.0", 1);
is($#results+1, 11, "search_logfile returned 11 results");

# sort by filenum so we can compare each to what it should be
@results = sort { $a->{'filenum'} <=> $b->{'filenum'} } @results;

# and convert the hashes to arrays for easy comparison
@results_arr = map { res2arr($_) } @results;

is_deeply(\@results_arr,
	[ [ '20071109010002', 'clihost', '/usr',	    0, 'TESTCONF002', 1,  'OK', '1'   ],
	  [ '20071109010002', 'clihost', '/my documents',   0, 'TESTCONF002', 2,  'OK', '1'   ],
	  [ '20071109010002', 'thatbox', '/var',	    1, 'TESTCONF002', 3,  'OK', '--'  ],
	  [ '20071109010002', 'clihost', '/home',	    0, 'TESTCONF002', 4,  'OK', '1/5' ],
	  [ '20071109010002', 'clihost', '/home',	    0, 'TESTCONF002', 5,  'OK', '2/5' ],
	  [ '20071109010002', 'clihost', '/home',	    0, 'TESTCONF002', 6,  'OK', '3/5' ],
	  [ '20071109010002', 'clihost', '/home',	    0, 'TESTCONF002', 7,  'OK', '4/5' ],
	  [ '20071109010002', 'clihost', '/home',	    0, 'TESTCONF002', 8,  'OK', '5/5' ],
	  [ '20071109010002', 'thatbox', '/u_lose',   2, 'TESTCONF002', 9,  '"Oh no!"', '1/4' ],
	  [ '20071109010002', 'thatbox', '/u_lose',   2, 'TESTCONF002', 10, '"Oh no!"', '2/4' ],
	  [ '20071109010002', 'thatbox', '/u_lose',   2, 'TESTCONF002', 11, '"Oh no!"', '3/4' ] ],
	  "results are correct");

my @filtered;
my @filtered_arr;

@filtered = Amanda::Logfile::dumps_match([@results], "thatbox", undef, undef, undef, 0);
is($#filtered+1, 4, "four results match 'thatbox'");
@filtered = sort { $a->{'filenum'} <=> $b->{'filenum'} } @filtered;

@filtered_arr = map { res2arr($_) } @filtered;

is_deeply(\@filtered_arr,
	[ [ '20071109010002', 'thatbox', '/var',      1, 'TESTCONF002', 3,  'OK',       '--' ],
	  [ '20071109010002', 'thatbox', '/u_lose',   2, 'TESTCONF002', 9,  '"Oh no!"', '1/4' ],
	  [ '20071109010002', 'thatbox', '/u_lose',   2, 'TESTCONF002', 10, '"Oh no!"', '2/4' ],
	  [ '20071109010002', 'thatbox', '/u_lose',   2, 'TESTCONF002', 11, '"Oh no!"', '3/4' ] ],
	  "results are  correct");

@filtered = Amanda::Logfile::dumps_match([@results], "thatbox", "/var", undef, undef, 0);
is($#filtered+1, 1, "only one result matches 'thatbox:/var'");

@filtered = Amanda::Logfile::dumps_match([@results], undef, undef, "20071109010002", undef, 0);
is($#filtered+1, 11, "all 11 results match '20071109010002'");

@filtered = Amanda::Logfile::dumps_match([@results], undef, undef, "20071109010002", undef, 1);
is($#filtered+1, 8, "of those, 8 results are 'OK'");

@filtered = Amanda::Logfile::dumps_match([@results], undef, undef, undef, "2", 0);
is($#filtered+1, 3, "3 results are at level 2");
