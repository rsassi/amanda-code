# Copyright (c) 2012 Zmanda, Inc.  All Rights Reserved.
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
# Sunnyvale, CA 94085, USA, or: http://www.zmanda.com

use lib '@amperldir@';

use strict;
use warnings;

use JSON -convert_blessed_universally;

use Amanda::JSON::RPC::Dispatcher;
use Amanda::Debug;
use Amanda::Changer;
use Amanda::Config;
use Amanda::Constants;
use Amanda::Device;
use Amanda::Disklist;
use Amanda::Tapelist;
use Amanda::Feature;
use Amanda::Header;
use Amanda::Holding;
use Amanda::Interactivity;
use Amanda::MainLoop;
use Amanda::Paths;
use Amanda::Process;
use Amanda::Util qw( :constants );
use Amanda::JSON::Config;
use Amanda::JSON::Tapelist;
use Amanda::JSON::Changer;
use Amanda::JSON::DB::Catalog;

Amanda::Util::setup_application("amjson-server", "server", $CONTEXT_CMDLINE);
Amanda::Config::config_init(0,undef);
Amanda::Util::finish_setup($RUNNING_AS_DUMPUSER);

use Data::Dumper;

my $rpc = Amanda::JSON::RPC::Dispatcher->new;


$rpc->register( 'Amanda::JSON::Config::getconf_byname', \&Amanda::JSON::Config::getconf_byname );
$rpc->register( 'Amanda::JSON::Config::config_dir_relative', \&Amanda::JSON::Config::config_dir_relative );

$rpc->register( 'Amanda::JSON::Tapelist::get', \&Amanda::JSON::Tapelist::get );
$rpc->register( 'Amanda::JSON::Tapelist::update', \&Amanda::JSON::Tapelist::update );
$rpc->register( 'Amanda::JSON::Tapelist::add', \&Amanda::JSON::Tapelist::add );
$rpc->register( 'Amanda::JSON::Tapelist::remove', \&Amanda::JSON::Tapelist::remove );

$rpc->register( 'Amanda::JSON::Changer::inventory', \&Amanda::JSON::Changer::inventory );
$rpc->register( 'Amanda::JSON::Changer::update', \&Amanda::JSON::Changer::update );
$rpc->register( 'Amanda::JSON::Changer::load', \&Amanda::JSON::Changer::load );
$rpc->register( 'Amanda::JSON::Changer::unload', \&Amanda::JSON::Changer::unload );

$rpc->register( 'Amanda::JSON::DB::Catalog::get_parts', \&Amanda::JSON::DB::Catalog::get_parts );
$rpc->register( 'Amanda::JSON::DB::Catalog::get_dumps', \&Amanda::JSON::DB::Catalog::get_dumps );

$rpc->to_app;
