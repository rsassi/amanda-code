# Copyright (c) 2013 Zmanda, Inc.  All Rights Reserved.
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
# Contact information: Zmanda Inc., 465 S. Mathilda Ave., Suite 300
# Sunnyvale, CA 94085, USA, or: http://www.zmanda.com

package Amanda::Rest::Amcheck;
use strict;
use warnings;

use Amanda::Config qw( :init :getconf config_dir_relative );
use Amanda::Debug;
use Amanda::Paths;
use Amanda::Rest::Configs;
use Symbol;
use Data::Dumper;
use JSON;
use IPC::Open3;

use vars qw(@ISA);

=head1 NAME

Amanda::Rest::Amdump -- Rest interface to Amanda::Amdump

=head1 INTERFACE

=over

=item Run amcheck

request:
  POST localhost:5000/amanda/v1.0/configs/:CONFIG/amcheck
    query arguments:
        host=HOST
        disk=DISK               #repeatable
        hostdisk=HOST|DISK      #repeatable
        server=0|1
        client=0|1
        local=0|1
        tape=0|1

reply:
  HTTP status: 200 Ok
  [
   {
      "code" : "2800027",
      "message" : "Amanda Tape Server Host Check",
      "severity" : "16",
      "source_filename" : "amcheck.c",
      "source_line" : "820"
   },
   {
      "code" : "2800028",
      "message" : "-----------------------------",
      "severity" : "16",
      "source_filename" : "amcheck.c",
      "source_line" : "821"
   },
   {
      "code" : "2800073",
      "message" : "Holding disk /amanda/h1/hdisk: 290584 MB disk space available, using 102400 MB as requested",
      "severity" : "16",
      "source_filename" : "amcheck.c",
      "source_line" : "1240"
   },
   {
      "code" : "123",
      "message" : "slot 1: volume 'TESTCONF-AA-vtapes-001'",
      "severity" : "16",
      "source_filename" : "amcheck.c",
      "source_line" : "749"
   },
   {
      "code" : "123",
      "message" : "Will write to volume 'TESTCONF-AA-vtapes-001' in slot 1.",
      "severity" : "16",
      "source_filename" : "amcheck.c",
      "source_line" : "749"
   },
   {
      "code" : "2800160",
      "message" : "Server check took 0.195 seconds",
      "severity" : "16",
      "source_filename" : "amcheck.c",
      "source_line" : "1746"
   },
   {
      "code" : "2800202",
      "message" : "Amanda Backup Client Hosts Check",
      "severity" : "16",
      "source_filename" : "amcheck.c",
      "source_line" : "2201"
   },
   {
      "code" : "2800203",
      "message" : "--------------------------------",
      "severity" : "16",
      "source_filename" : "amcheck.c",
      "source_line" : "2202"
   },
   {
      "code" : "2800204",
      "message" : "Client check: 1 hosts checked in 0.108 seconds.  0 problems found.",
      "severity" : "16",
      "source_filename" : "amcheck.c",
      "source_line" : "2237"
   },
   {
      "code" : "2800016",
      "message" : "(brought to you by Amanda 4.0.0alpha)",
      "severity" : "16",
      "source_filename" : "amcheck.c",
      "source_line" : "457"
   }
  ]

=back

=cut

sub check {
    my %params = @_;

    my @amcheck_args;
    my @result_messages = Amanda::Rest::Configs::config_init(@_);
    return \@result_messages if @result_messages;

    my $user_msg = sub {
	my $msg = shift;
	push @result_messages, $msg;
    };

    if (defined $params{'server'} and $params{'server'}) {
	push @amcheck_args, '-s';
    }
    if (defined $params{'local'} and $params{'local'}) {
	push @amcheck_args, '-l';
    }
    if (defined $params{'tape'} and $params{'tape'}) {
	push @amcheck_args, '-t';
    }
    if (defined $params{'client'} and $params{'client'}) {
	push @amcheck_args, '-s';
    }
    if (defined $params{'client-verbose'} and $params{'client-verbose'}) {
	push @amcheck_args, '--client-verbose';
    }
    push @amcheck_args, '--message';
    push @amcheck_args, '--exact-match';
    push @amcheck_args, $params{'CONF'};
    if (defined($params{'host'})) {
	my @hostdisk;
	if (defined($params{'disk'})) {
	    if (ref($params{'disk'}) eq 'ARRAY') {
		foreach my $disk (@{$params{'disk'}}) {
		    push @hostdisk, $params{'host'}, $disk;
		}
	    } else {
		push @hostdisk, $params{'host'}, $params{'disk'};
	    }
	} else {
	    push @hostdisk, $params{'host'};
	}
	push @amcheck_args, @hostdisk;
    }

    # fork the amcheck process
    my($wtr, $rdr);
    open3($wtr, $rdr, undef, "$Amanda::Paths::sbindir/amcheck", @amcheck_args);
    close($wtr);

    #read stdout in a buffer
    my $buf = "[ ";
    while (my $line = <$rdr>) {
	$buf .= $line;
    }
    $buf .= " { } ]";
    close($rdr);

    #convert JSON buffer to perl object
    my $ret = decode_json $buf;

    #return perl object
    return $ret
}

1;
