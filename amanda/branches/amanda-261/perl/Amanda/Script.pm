# vim:ft=perl
# Copyright (c) 2005-2008 Zmanda, Inc.  All Rights Reserved.
#
# This library is free software; you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License version 2.1 as
# published by the Free Software Foundation.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
# License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this library; if not, write to the Free Software Foundation,
# Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
#
# Contact information: Zmanda Inc., 465 S Mathlida Ave, Suite 300
# Sunnyvale, CA 94086, USA, or: http://www.zmanda.com

package Amanda::Script;
use base qw(Amanda::Script_App);

use strict;
use warnings;

=head1 NAME

Amanda::Script - perl utility functions for Scripts.

=head1 SYNOPSIS

  package Amanda::Script::my_script;
  use base qw(Amanda::Script);

  sub new {
    my $class = shift;
    my ($execute_where, $foo, $bar) = @_;
    my $self = $class->SUPER::new($execute_where);
    $self->{'execute_where'} = $execute_where;
    $self->{'foo'} = $foo;
    $self->{'bar'} = $bar;

    return $self;
  }

  # Define all command_* subs that you need, e.g.,
  sub command_pre_dle_amcheck {
    my $self = shift;
    # ...
  }

  package main;

  # .. parse arguments ..
  my $script = Amanda::Script::my_script->new($opt_execute_where, $opt_foo, $opt_bar);
  $script->do($cmd);

=cut

sub new {
    my $class = shift;
    my $execute_where = shift;

    my $self = Amanda::Script_App::new($class, $execute_where, "script", @_);

    $self->{known_commands} = {
        support             => 1,
        pre_dle_amcheck     => 1,
        pre_host_amcheck    => 1,
        post_dle_amcheck    => 1,
        post_host_amcheck   => 1,
        pre_dle_estimate    => 1,
        pre_host_estimate   => 1,
        post_dle_estimate   => 1,
        post_host_estimate  => 1,
        pre_dle_backup      => 1,
        pre_host_backup     => 1,
        post_dle_backup     => 1,
        post_host_backup    => 1,
        pre_recover         => 1,
        post_recover        => 1,
        pre_level_recover   => 1,
        post_level_recover  => 1,
        inter_level_recover => 1,
    };
    return $self;
}


1;
