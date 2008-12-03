# Copyright (c) 2005-2008 Zmanda Inc.  All Rights Reserved.
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
# Contact information: Zmanda Inc, 465 S Mathlida Ave, Suite 300
# Sunnyvale, CA 94086, USA, or: http://www.zmanda.com

use Test::More tests => 4;

use lib "@amperldir@";
use Installcheck::Config;
use Installcheck::Run qw(run run_err $diskname);
use Amanda::Config qw( :init config_dir_relative );
use Amanda::Paths;
use Amanda::Tapelist;

my $testconf;

$testconf = Installcheck::Run::setup();
$testconf->add_param('label_new_tapes', '"TESTCONF%%"');
$testconf->add_param('usetimestamps', 'no');
$testconf->add_dle("localhost $diskname installcheck-test");
$testconf->write();

ok(run('amdump', 'TESTCONF'), "amdump ran successfully");

config_init($CONFIG_INIT_EXPLICIT_NAME, 'TESTCONF');
my $tapelist = Amanda::Tapelist::read_tapelist(config_dir_relative("tapelist"));
ok($tapelist->lookup_tapelabel('TESTCONF01')) or diag("could not lookup tape");

ok(run('amrmtape', 'TESTCONF', 'TESTCONF01'), "amrmtape runs successfully")
    or diag(join("\n", 'stdout:', $Installcheck::Run::stdout, '', 'stderr:', $Installcheck::Run::stderr)
);

$tapelist = Amanda::Tapelist::read_tapelist(config_dir_relative("tapelist"));
ok(!$tapelist->lookup_tapelabel('TESTCONF01')) or diag("succesfully looked up tape that should have been removed");

Installcheck::Run::cleanup();
