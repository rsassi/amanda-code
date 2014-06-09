# Copyright (c) 2014 Zmanda, Inc.  All Rights Reserved.
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
# Contact information: Zmanda Inc, 465 S. Mathilda Ave., Suite 300
# Sunnyvale, CA 94086, USA, or: http://www.zmanda.com

use Test::More;
use File::Path;
use strict;
use warnings;

use lib '@amperldir@';
use Installcheck;
use Installcheck::Dumpcache;
use Installcheck::Config;
use Amanda::Paths;
use Amanda::Device qw( :constants );
use Amanda::Debug;
use Amanda::MainLoop;
use Amanda::Config qw( :init :getconf config_dir_relative );
use Amanda::Changer;

eval 'use Installcheck::Rest;';
if ($@) {
    plan skip_all => "Can't load Installcheck::Rest: $@";
    exit 1;
}

# set up debugging so debug output doesn't interfere with test results
Amanda::Debug::dbopen("installcheck");
Installcheck::log_test_output();

# and disable Debug's die() and warn() overrides
Amanda::Debug::disable_die_override();

my $rest = Installcheck::Rest->new();
if ($rest->{'error'}) {
   plan skip_all => "Can't start JSON Rest server: $rest->{'error'}: see " . Amanda::Debug::dbfn();
   exit 1;
}
plan tests => 12;

my $reply;

my $amperldir = $Amanda::Paths::amperldir;
my $testconf;

$testconf = Installcheck::Run::setup();
$testconf->write();
config_init($CONFIG_INIT_EXPLICIT_NAME, "TESTCONF");

#CODE 1600001
$reply = $rest->get("http://localhost:5001/amanda/v1.0/configs/TESTCONF/labels");
is_deeply (Installcheck::Rest::remove_source_line($reply),
    { body =>
        [ {	'source_filename' => "$amperldir/Amanda/Rest/Labels.pm",
		'tles' => [],
		'severity' => '16',
		'message' => 'List of labels',
		'code' => '1600001'
	  },
        ],
      http_code => 200,
    },
    "No config");


my $tapelist_data = <<EOF;
20140527000000 vtape-AA-000 reuse META:AA BLOCKSIZE:32 POOL:my_vtapes STORAGE:my_vtapes CONFIG:TESTCONF #comment000
20140527000001 vtape-AA-001 reuse META:AA BLOCKSIZE:32 POOL:my_vtapes STORAGE:my_vtapes CONFIG:TESTCONF #comment001
20140527000002 vtape-AB-002 reuse META:AB BLOCKSIZE:32 POOL:my_vtapes STORAGE:my_vtapes CONFIG:TESTCONF #comment002
20140527000003 vtape-AA-003 no-reuse META:AA BLOCKSIZE:32 POOL:my_vtapes STORAGE:my_vtapes CONFIG:TESTCONF #comment003
20140527000004 vtape-AA-004 no-reuse META:AA BLOCKSIZE:32 POOL:my_vtapes STORAGE:my_vtapes CONFIG:TESTCONF #comment004
20140527000005 vtape-AB-005 no-reuse META:AB BLOCKSIZE:32 POOL:my_vtapes STORAGE:my_vtapes CONFIG:TESTCONF #comment005
20140527000100 robot-BAR100 reuse BARCODE:BAR100 BLOCKSIZE:32 POOL:my_robot STORAGE:my_robot CONFIG:TESTCONF
20140527000101 robot-BAR101 reuse BARCODE:BAR101 BLOCKSIZE:32 POOL:my_robot STORAGE:my_robot CONFIG:TESTCONF
20140527000102 robot-BAR102 no-reuse BARCODE:BAR102 BLOCKSIZE:32 POOL:my_robot STORAGE:my_robot CONFIG:TESTCONF
20140527000103 robot-BAR103 no-reuse BARCODE:BAR103 BLOCKSIZE:32 POOL:my_robot STORAGE:my_robot CONFIG:TESTCONF
20140527000200 tape-200 reuse BLOCKSIZE:32 POOL:my_tape STORAGE:my_tape CONFIG:TESTCONF2
20140527000201 tape-201 reuse BLOCKSIZE:32 POOL:my_tape STORAGE:my_tape CONFIG:TESTCONF2
EOF

my $tlf = Amanda::Config::config_dir_relative(getconf($CNF_TAPELIST));
open TAPELIST, ">$tlf";
print TAPELIST $tapelist_data;
close TAPELIST;

#CODE 1600001
$reply = $rest->get("http://localhost:5001/amanda/v1.0/configs/TESTCONF/labels");
is_deeply (Installcheck::Rest::remove_source_line($reply),
    { body =>
        [ {	'source_filename' => "$amperldir/Amanda/Rest/Labels.pm",
		'tles' => [
		  { 'position' => 1,
		    'datestamp' => 20140527000201,
		    'label' => 'tape-201',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_tape',
		    'storage' => 'my_tape',
		    'config' => 'TESTCONF2',
		    'comment' => undef, },
		  { 'position' => 2,
		    'datestamp' => 20140527000200,
		    'label' => 'tape-200',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_tape',
		    'storage' => 'my_tape',
		    'config' => 'TESTCONF2',
		    'comment' => undef, },
		  { 'position' => 3,
		    'datestamp' => 20140527000103,
		    'label' => 'robot-BAR103',
		    'reuse' => '0',
		    'barcode' => 'BAR103',
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_robot',
		    'storage' => 'my_robot',
		    'config' => 'TESTCONF',
		    'comment' => undef, },
		  { 'position' => 4,
		    'datestamp' => 20140527000102,
		    'label' => 'robot-BAR102',
		    'reuse' => '0',
		    'barcode' => 'BAR102',
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_robot',
		    'storage' => 'my_robot',
		    'config' => 'TESTCONF',
		    'comment' => undef, },
		  { 'position' => 5,
		    'datestamp' => 20140527000101,
		    'label' => 'robot-BAR101',
		    'reuse' => '1',
		    'barcode' => 'BAR101',
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_robot',
		    'storage' => 'my_robot',
		    'config' => 'TESTCONF',
		    'comment' => undef, },
		  { 'position' => 6,
		    'datestamp' => 20140527000100,
		    'label' => 'robot-BAR100',
		    'reuse' => '1',
		    'barcode' => 'BAR100',
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_robot',
		    'storage' => 'my_robot',
		    'config' => 'TESTCONF',
		    'comment' => undef, },
		  { 'position' => 7,
		    'datestamp' => 20140527000005,
		    'label' => 'vtape-AB-005',
		    'reuse' => '0',
		    'barcode' => undef,
		    'meta' => 'AB',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment005', },
		  { 'position' => 8,
		    'datestamp' => 20140527000004,
		    'label' => 'vtape-AA-004',
		    'reuse' => '0',
		    'barcode' => undef,
		    'meta' => 'AA',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment004', },
		  { 'position' => 9,
		    'datestamp' => 20140527000003,
		    'label' => 'vtape-AA-003',
		    'reuse' => '0',
		    'barcode' => undef,
		    'meta' => 'AA',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment003', },
		  { 'position' => 10,
		    'datestamp' => 20140527000002,
		    'label' => 'vtape-AB-002',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => 'AB',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment002', },
		  { 'position' => 11,
		    'datestamp' => 20140527000001,
		    'label' => 'vtape-AA-001',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => 'AA',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment001', },
		  { 'position' => 12,
		    'datestamp' => 20140527000000,
		    'label' => 'vtape-AA-000',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => 'AA',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment000', },
		],
		'severity' => '16',
		'message' => 'List of labels',
		'code' => '1600001'
	  },
        ],
      http_code => 200,
    },
    "All Dles");

#CODE 1600001
$reply = $rest->get("http://localhost:5001/amanda/v1.0/configs/TESTCONF/labels?config=TESTCONF2");
is_deeply (Installcheck::Rest::remove_source_line($reply),
    { body =>
        [ {	'source_filename' => "$amperldir/Amanda/Rest/Labels.pm",
		'tles' => [
		  { 'position' => 1,
		    'datestamp' => 20140527000201,
		    'label' => 'tape-201',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_tape',
		    'storage' => 'my_tape',
		    'config' => 'TESTCONF2',
		    'comment' => undef, },
		  { 'position' => 2,
		    'datestamp' => 20140527000200,
		    'label' => 'tape-200',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_tape',
		    'storage' => 'my_tape',
		    'config' => 'TESTCONF2',
		    'comment' => undef, },
		],
		'severity' => '16',
		'message' => 'List of labels',
		'code' => '1600001'
	  },
        ],
      http_code => 200,
    },
    "config=TESTCONF2");

#CODE 1600001
$reply = $rest->get("http://localhost:5001/amanda/v1.0/configs/TESTCONF/labels?storage=my_robot");
is_deeply (Installcheck::Rest::remove_source_line($reply),
    { body =>
        [ {	'source_filename' => "$amperldir/Amanda/Rest/Labels.pm",
		'tles' => [
		  { 'position' => 3,
		    'datestamp' => 20140527000103,
		    'label' => 'robot-BAR103',
		    'reuse' => '0',
		    'barcode' => 'BAR103',
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_robot',
		    'storage' => 'my_robot',
		    'config' => 'TESTCONF',
		    'comment' => undef, },
		  { 'position' => 4,
		    'datestamp' => 20140527000102,
		    'label' => 'robot-BAR102',
		    'reuse' => '0',
		    'barcode' => 'BAR102',
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_robot',
		    'storage' => 'my_robot',
		    'config' => 'TESTCONF',
		    'comment' => undef, },
		  { 'position' => 5,
		    'datestamp' => 20140527000101,
		    'label' => 'robot-BAR101',
		    'reuse' => '1',
		    'barcode' => 'BAR101',
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_robot',
		    'storage' => 'my_robot',
		    'config' => 'TESTCONF',
		    'comment' => undef, },
		  { 'position' => 6,
		    'datestamp' => 20140527000100,
		    'label' => 'robot-BAR100',
		    'reuse' => '1',
		    'barcode' => 'BAR100',
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_robot',
		    'storage' => 'my_robot',
		    'config' => 'TESTCONF',
		    'comment' => undef, },
		],
		'severity' => '16',
		'message' => 'List of labels',
		'code' => '1600001'
	  },
        ],
      http_code => 200,
    },
    "storage=my_robot");

#CODE 1600001
$reply = $rest->get("http://localhost:5001/amanda/v1.0/configs/TESTCONF/labels?meta=AA");
is_deeply (Installcheck::Rest::remove_source_line($reply),
    { body =>
        [ {	'source_filename' => "$amperldir/Amanda/Rest/Labels.pm",
		'tles' => [
		  { 'position' => 8,
		    'datestamp' => 20140527000004,
		    'label' => 'vtape-AA-004',
		    'reuse' => '0',
		    'barcode' => undef,
		    'meta' => 'AA',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment004', },
		  { 'position' => 9,
		    'datestamp' => 20140527000003,
		    'label' => 'vtape-AA-003',
		    'reuse' => '0',
		    'barcode' => undef,
		    'meta' => 'AA',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment003', },
		  { 'position' => 11,
		    'datestamp' => 20140527000001,
		    'label' => 'vtape-AA-001',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => 'AA',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment001', },
		  { 'position' => 12,
		    'datestamp' => 20140527000000,
		    'label' => 'vtape-AA-000',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => 'AA',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment000', },
		],
		'severity' => '16',
		'message' => 'List of labels',
		'code' => '1600001'
	  },
        ],
      http_code => 200,
    },
    "meta=AA");

#CODE 1600001
$reply = $rest->get("http://localhost:5001/amanda/v1.0/configs/TESTCONF/labels?pool=my_vtapes");
is_deeply (Installcheck::Rest::remove_source_line($reply),
    { body =>
        [ {	'source_filename' => "$amperldir/Amanda/Rest/Labels.pm",
		'tles' => [
		  { 'position' => 7,
		    'datestamp' => 20140527000005,
		    'label' => 'vtape-AB-005',
		    'reuse' => '0',
		    'barcode' => undef,
		    'meta' => 'AB',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment005', },
		  { 'position' => 8,
		    'datestamp' => 20140527000004,
		    'label' => 'vtape-AA-004',
		    'reuse' => '0',
		    'barcode' => undef,
		    'meta' => 'AA',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment004', },
		  { 'position' => 9,
		    'datestamp' => 20140527000003,
		    'label' => 'vtape-AA-003',
		    'reuse' => '0',
		    'barcode' => undef,
		    'meta' => 'AA',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment003', },
		  { 'position' => 10,
		    'datestamp' => 20140527000002,
		    'label' => 'vtape-AB-002',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => 'AB',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment002', },
		  { 'position' => 11,
		    'datestamp' => 20140527000001,
		    'label' => 'vtape-AA-001',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => 'AA',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment001', },
		  { 'position' => 12,
		    'datestamp' => 20140527000000,
		    'label' => 'vtape-AA-000',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => 'AA',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment000', },
		],
		'severity' => '16',
		'message' => 'List of labels',
		'code' => '1600001'
	  },
        ],
      http_code => 200,
    },
    "pool=my_vtapes");

#CODE 1600001
$reply = $rest->get("http://localhost:5001/amanda/v1.0/configs/TESTCONF/labels?reuse=1");
is_deeply (Installcheck::Rest::remove_source_line($reply),
    { body =>
        [ {	'source_filename' => "$amperldir/Amanda/Rest/Labels.pm",
		'tles' => [
		  { 'position' => 1,
		    'datestamp' => 20140527000201,
		    'label' => 'tape-201',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_tape',
		    'storage' => 'my_tape',
		    'config' => 'TESTCONF2',
		    'comment' => undef, },
		  { 'position' => 2,
		    'datestamp' => 20140527000200,
		    'label' => 'tape-200',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_tape',
		    'storage' => 'my_tape',
		    'config' => 'TESTCONF2',
		    'comment' => undef, },
		  { 'position' => 5,
		    'datestamp' => 20140527000101,
		    'label' => 'robot-BAR101',
		    'reuse' => '1',
		    'barcode' => 'BAR101',
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_robot',
		    'storage' => 'my_robot',
		    'config' => 'TESTCONF',
		    'comment' => undef, },
		  { 'position' => 6,
		    'datestamp' => 20140527000100,
		    'label' => 'robot-BAR100',
		    'reuse' => '1',
		    'barcode' => 'BAR100',
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_robot',
		    'storage' => 'my_robot',
		    'config' => 'TESTCONF',
		    'comment' => undef, },
		  { 'position' => 10,
		    'datestamp' => 20140527000002,
		    'label' => 'vtape-AB-002',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => 'AB',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment002', },
		  { 'position' => 11,
		    'datestamp' => 20140527000001,
		    'label' => 'vtape-AA-001',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => 'AA',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment001', },
		  { 'position' => 12,
		    'datestamp' => 20140527000000,
		    'label' => 'vtape-AA-000',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => 'AA',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment000', },
		],
		'severity' => '16',
		'message' => 'List of labels',
		'code' => '1600001'
	  },
        ],
      http_code => 200,
    },
    "resue=1");

#CODE 1600001
$reply = $rest->get("http://localhost:5001/amanda/v1.0/configs/TESTCONF/labels?reuse=0");
is_deeply (Installcheck::Rest::remove_source_line($reply),
    { body =>
        [ {	'source_filename' => "$amperldir/Amanda/Rest/Labels.pm",
		'tles' => [
		  { 'position' => 3,
		    'datestamp' => 20140527000103,
		    'label' => 'robot-BAR103',
		    'reuse' => '0',
		    'barcode' => 'BAR103',
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_robot',
		    'storage' => 'my_robot',
		    'config' => 'TESTCONF',
		    'comment' => undef, },
		  { 'position' => 4,
		    'datestamp' => 20140527000102,
		    'label' => 'robot-BAR102',
		    'reuse' => '0',
		    'barcode' => 'BAR102',
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_robot',
		    'storage' => 'my_robot',
		    'config' => 'TESTCONF',
		    'comment' => undef, },
		  { 'position' => 7,
		    'datestamp' => 20140527000005,
		    'label' => 'vtape-AB-005',
		    'reuse' => '0',
		    'barcode' => undef,
		    'meta' => 'AB',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment005', },
		  { 'position' => 8,
		    'datestamp' => 20140527000004,
		    'label' => 'vtape-AA-004',
		    'reuse' => '0',
		    'barcode' => undef,
		    'meta' => 'AA',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment004', },
		  { 'position' => 9,
		    'datestamp' => 20140527000003,
		    'label' => 'vtape-AA-003',
		    'reuse' => '0',
		    'barcode' => undef,
		    'meta' => 'AA',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment003', },
		],
		'severity' => '16',
		'message' => 'List of labels',
		'code' => '1600001'
	  },
        ],
      http_code => 200,
    },
    "reuse=0");

#CODE 1600001
$reply = $rest->get("http://localhost:5001/amanda/v1.0/configs/TESTCONF/labels?storage=my_vtapes&config=TESTCONF&reuse=1");
is_deeply (Installcheck::Rest::remove_source_line($reply),
    { body =>
        [ {	'source_filename' => "$amperldir/Amanda/Rest/Labels.pm",
		'tles' => [
		  { 'position' => 10,
		    'datestamp' => 20140527000002,
		    'label' => 'vtape-AB-002',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => 'AB',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment002', },
		  { 'position' => 11,
		    'datestamp' => 20140527000001,
		    'label' => 'vtape-AA-001',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => 'AA',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment001', },
		  { 'position' => 12,
		    'datestamp' => 20140527000000,
		    'label' => 'vtape-AA-000',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => 'AA',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment000', },
		],
		'severity' => '16',
		'message' => 'List of labels',
		'code' => '1600001'
	  },
        ],
      http_code => 200,
    },
    "storage=my_vtapes&config=TESTCONF&reuse=1");

#CODE 1600001
$reply = $rest->get("http://localhost:5001/amanda/v1.0/configs/TESTCONF/labels?storage=my_vtapes&config=TESTCONF&reuse=1&meta=AA");
is_deeply (Installcheck::Rest::remove_source_line($reply),
    { body =>
        [ {	'source_filename' => "$amperldir/Amanda/Rest/Labels.pm",
		'tles' => [
		  { 'position' => 11,
		    'datestamp' => 20140527000001,
		    'label' => 'vtape-AA-001',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => 'AA',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment001', },
		  { 'position' => 12,
		    'datestamp' => 20140527000000,
		    'label' => 'vtape-AA-000',
		    'reuse' => '1',
		    'barcode' => undef,
		    'meta' => 'AA',
		    'blocksize' => '32',
		    'pool' => 'my_vtapes',
		    'storage' => 'my_vtapes',
		    'config' => 'TESTCONF',
		    'comment' => 'comment000', },
		],
		'severity' => '16',
		'message' => 'List of labels',
		'code' => '1600001'
	  },
        ],
      http_code => 200,
    },
    "storage=my_vtapes&config=TESTCONF&reuse=1&meta=AA");

#CODE 1600001
$reply = $rest->get("http://localhost:5001/amanda/v1.0/configs/TESTCONF/labels?pool=my_robot&config=TESTCONF");
is_deeply (Installcheck::Rest::remove_source_line($reply),
    { body =>
        [ {	'source_filename' => "$amperldir/Amanda/Rest/Labels.pm",
		'tles' => [
		  { 'position' => 3,
		    'datestamp' => 20140527000103,
		    'label' => 'robot-BAR103',
		    'reuse' => '0',
		    'barcode' => 'BAR103',
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_robot',
		    'storage' => 'my_robot',
		    'config' => 'TESTCONF',
		    'comment' => undef, },
		  { 'position' => 4,
		    'datestamp' => 20140527000102,
		    'label' => 'robot-BAR102',
		    'reuse' => '0',
		    'barcode' => 'BAR102',
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_robot',
		    'storage' => 'my_robot',
		    'config' => 'TESTCONF',
		    'comment' => undef, },
		  { 'position' => 5,
		    'datestamp' => 20140527000101,
		    'label' => 'robot-BAR101',
		    'reuse' => '1',
		    'barcode' => 'BAR101',
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_robot',
		    'storage' => 'my_robot',
		    'config' => 'TESTCONF',
		    'comment' => undef, },
		  { 'position' => 6,
		    'datestamp' => 20140527000100,
		    'label' => 'robot-BAR100',
		    'reuse' => '1',
		    'barcode' => 'BAR100',
		    'meta' => undef,
		    'blocksize' => '32',
		    'pool' => 'my_robot',
		    'storage' => 'my_robot',
		    'config' => 'TESTCONF',
		    'comment' => undef, },
		],
		'severity' => '16',
		'message' => 'List of labels',
		'code' => '1600001'
	  },
        ],
      http_code => 200,
    },
    "pool=my_robot&config=TESTCONF");

#CODE 1600001
$reply = $rest->get("http://localhost:5001/amanda/v1.0/configs/TESTCONF/labels?pool=my_robot&config=TESTCONF2");
is_deeply (Installcheck::Rest::remove_source_line($reply),
    { body =>
        [ {	'source_filename' => "$amperldir/Amanda/Rest/Labels.pm",
		'tles' => [
		],
		'severity' => '16',
		'message' => 'List of labels',
		'code' => '1600001'
	  },
        ],
      http_code => 200,
    },
    "pool=my_robot&config=TESTCONF2");

#diag("reply: " . Data::Dumper::Dumper($reply));

$rest->stop();
