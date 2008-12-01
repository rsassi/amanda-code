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

package Amanda::BigIntCompat;

use strict;
use warnings;
use overload;
use Math::BigInt;

=head1 NAME

Amanda::BigIntCompat -- make C<Math::BigInt> behave consistently

=head1 SYNOPSIS

  use Amanda::BigIntCompat;
  use Math::BigInt;

  my $bn = Math::BigInt->new(1);
  print "okay\n" if $bn eq "1";

=head1 API STATUS

Stable

=head1 INTERFACE

This module will modify C<Math::BigInt> to hide inconsistent behaviors across
Perl versions. Spefically, it handles the following.

=over

=item stringification

Older versions of C<Math::BigInt>, like the one shipped with Perl 5.6.1,
stringify positive numbers with a leading C<+> (e.g. C<+1> instead of C<1>).

=back

=cut

my $test_num = Math::BigInt->new(1);
our $stringify = overload::Method($test_num, '""');

if ($test_num =~ /^\+/) {
    eval '
        package Math::BigInt;
        use overload \'""\' => sub {
            my $str = $Amanda::BigIntCompat::stringify->(@_);
            $str =~ s/^\+//;
        };
    ';
}

1;
