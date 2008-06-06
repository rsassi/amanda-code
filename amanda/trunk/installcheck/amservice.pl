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

use Test::More tests => 1;

use lib "@amperldir@";
use Installcheck::Run qw( run run_get );
use Amanda::Paths;

my $testconf = Installcheck::Run::setup();

# a simple run of amservice to begin with
like(run_get('amservice', 'localhost', 'local', 'noop', '-f', '/dev/null'),
    qr/^OPTIONS features=/,
    "amservice runs noop successfully");

Installcheck::Run::cleanup();
