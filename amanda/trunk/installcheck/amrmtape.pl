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

use Test::More tests => 26;

use lib "@amperldir@";
use File::Find;
use Amanda::Config qw( :init :getconf config_dir_relative );
use Amanda::Device qw( :constants );
use Amanda::Paths;
use Amanda::Tapelist;
use Installcheck::Config;
use Installcheck::Run qw(run run_err $diskname);
use Installcheck::Dumpcache;

sub proc_diag {
    diag(join("\n", $?,
        'stdout:', $Installcheck::Run::stdout, '',
        'stderr:', $Installcheck::Run::stderr));
}

# note: assumes the config is already loaded and takes a config param
# to get as a directory and then count all the files in
sub dir_file_count {
    my $conf_param = shift @_;
    my $dir_name = getconf($conf_param);

    my $num_files = 0;
    my $opts = {
        'wanted' => sub {
            # ignore directories
            return if -d $File::Find::name;
            $num_files++;
        },
    };

    find($opts, $dir_name);
    $num_files;
}

my $dev;
my ($idx_count_pre, $idx_count_post);

Installcheck::Dumpcache::load("notimestamps");

config_init($CONFIG_INIT_EXPLICIT_NAME, 'TESTCONF');
my $tapelist = Amanda::Tapelist::read_tapelist(config_dir_relative("tapelist"));
ok($tapelist->lookup_tapelabel('TESTCONF01'), "looked up tape after dump");

$idx_count_pre = dir_file_count($CNF_INDEXDIR);

ok(run('amrmtape', 'TESTCONF', 'TESTCONF01'), "amrmtape runs successfully")
    or proc_diag();

$idx_count_post = dir_file_count($CNF_INDEXDIR);
is($idx_count_post, $idx_count_pre, "number of index files before and after is the same");

$tapelist = Amanda::Tapelist::read_tapelist(config_dir_relative("tapelist"));
ok(!$tapelist->lookup_tapelabel('TESTCONF01'),
     "should fail to look up tape that should has been removed");

$dev = Amanda::Device->new('file:' . Installcheck::Run::vtape_dir());

ok($dev->start($ACCESS_READ, undef, undef),
    "start device in read mode")
    or diag($dev->error_or_status());

ok($dev->finish(),
   "finish device after starting")
    or diag($dev->error_or_status());

# test --cleanup

Installcheck::Dumpcache::load("notimestamps");

$idx_count_pre = dir_file_count($CNF_INDEXDIR);

ok(run('amrmtape', '--cleanup', 'TESTCONF', 'TESTCONF01'),
    "amrmtape runs successfully with --cleanup")
     or proc_diag();

$idx_count_post = dir_file_count($CNF_INDEXDIR);
isnt($idx_count_post, $idx_count_pre, "number of index files before and after is different");

$tapelist = Amanda::Tapelist::read_tapelist(config_dir_relative("tapelist"));
ok(!$tapelist->lookup_tapelabel('TESTCONF01'),
     "succesfully looked up tape that should have been removed after --cleanup");

$dev = Amanda::Device->new('file:' . Installcheck::Run::vtape_dir());

ok($dev->start($ACCESS_READ, undef, undef),
    "start device in read mode")
    or diag($dev->error_or_status());

ok($dev->finish(),
   "finish device after starting")
    or diag($dev->error_or_status());

# test --erase

Installcheck::Dumpcache::load("notimestamps");

$idx_count_pre = dir_file_count($CNF_INDEXDIR);

ok(run('amrmtape', '--erase', 'TESTCONF', 'TESTCONF01'),
    "amrmtape runs successfully with --erase")
    or proc_diag();

$idx_count_post = dir_file_count($CNF_INDEXDIR);
is($idx_count_post, $idx_count_pre, "number of index files before and after is the same");

$tapelist = Amanda::Tapelist::read_tapelist(config_dir_relative("tapelist"));
ok(!$tapelist->lookup_tapelabel('TESTCONF01'),
     "succesfully looked up tape that should have been removed after --erase");

$dev = Amanda::Device->new('file:' . Installcheck::Run::vtape_dir());

ok(!$dev->start($ACCESS_READ, undef, undef),
    "start device in read mode fails")
    or diag($dev->error_or_status());

# just in case the above does start the device
ok($dev->finish(),
   "finish device (just in case)")
    or diag($dev->error_or_status());

# test --keep-label

Installcheck::Dumpcache::load("notimestamps");

$idx_count_pre = dir_file_count($CNF_INDEXDIR);

ok(run('amrmtape', '--keep-label', 'TESTCONF', 'TESTCONF01'),
   "amrmtape runs successfully with --keep-label")
    or proc_diag();

$idx_count_post = dir_file_count($CNF_INDEXDIR);
is($idx_count_post, $idx_count_pre, "number of index files before and after is the same");

$tapelist = Amanda::Tapelist::read_tapelist(config_dir_relative("tapelist"));
ok($tapelist->lookup_tapelabel('TESTCONF01'),
     "succesfully looked up tape that should still be there");

$dev = Amanda::Device->new('file:' . Installcheck::Run::vtape_dir());

ok($dev->start($ACCESS_READ, undef, undef),
    "start device in read mode")
    or diag($dev->error_or_status());

ok($dev->finish(),
   "finish device after starting")
    or diag($dev->error_or_status());

# test --dryrun --erase --cleanup

Installcheck::Dumpcache::load("notimestamps");

$idx_count_pre = dir_file_count($CNF_INDEXDIR);

ok(run('amrmtape', '--dryrun', '--erase', '--cleanup', 'TESTCONF', 'TESTCONF01'),
    "amrmtape runs successfully with --dryrun --erase --cleanup")
    or proc_diag();

$idx_count_post = dir_file_count($CNF_INDEXDIR);
is($idx_count_post, $idx_count_pre, "number of index files before and after is the same");

$tapelist = Amanda::Tapelist::read_tapelist(config_dir_relative("tapelist"));
ok($tapelist->lookup_tapelabel('TESTCONF01'),
     "succesfully looked up tape that should still be there");

$dev = Amanda::Device->new('file:' . Installcheck::Run::vtape_dir());

ok($dev->start($ACCESS_READ, undef, undef),
    "start device in read mode")
    or diag($dev->error_or_status());

ok($dev->finish(),
   "finish device after starting")
    or diag($dev->error_or_status());

Installcheck::Run::cleanup();
