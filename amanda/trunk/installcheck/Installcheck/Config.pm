# vim:ft=perl
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

package Installcheck::Config;
use Amanda::Paths;
use Amanda::Constants;
use File::Path;
use Carp;

=head1 NAME

Installcheck::Config - set up amanda configurations for installcheck testing

=head1 SYNOPSIS

  use Installcheck::Config;

  my $testconf = Installcheck::Config->new();
  $testconf->add_param("runtapes", "5");
  $testconf->add_subsec("tapetype", "DUCKTAPE", { length => "10G", filemark => "4096k" });
  # ...
  $testconf->write();

The resulting configuration is always named "TESTCONF".  The basic
configuration contains only a few parameters that are necessary
just to run Amanda applications in the test environment.  It also
contains a tapetype, C<TEST-TAPE>.  To change tapetype parameters,
call C<$cf->add_tapetype> with a new definition of C<TEST-TAPE>.

Note that it's quite possible to produce an invalid configuration with this
package (and, in fact, some of the tests do just that).

=head1 WARNING

Using this module I<will> destroy any existing configuration named
TESTDIR.  I<Please> do not use this on a production machine!

=head1 FUNCTIONS

=over

=item C<new()>

Create a new configuration object

=cut

sub new {
    my $class = shift;

    # An instance is a blessed hash containing parameters.  Start with
    # some defaults to make sure things run.
    my $infofile = "$CONFIG_DIR/TESTCONF/curinfo";
    my $logdir = "$CONFIG_DIR/TESTCONF/log";
    my $indexdir = "$CONFIG_DIR/TESTCONF/index";

    my $self = {
	'infofile' => $infofile,
	'logdir' => $logdir,
	'indexdir' => $indexdir,

	# Global params are stored as an arrayref, so that the same declaration
	# can appear multiple times
	'params' => [
	    'dumpuser' => '"' . (getpwuid($<))[0] . '"', # current username

	    # These dirs are under CONFIG_DIR just for ease of destruction.
	    # This is not a recommended layout!
	    'infofile' => "\"$infofile\"",
	    'logdir' => "\"$logdir\"",
	    'indexdir' => "\"$indexdir\"",

	    'tapetype' => '"TEST-TAPE"',
	],

	# Subsections are stored as a hashref of arrayrefs, keyed by
	# subsection name

	'tapetypes' => {
	    'TEST-TAPE' => [
		'length' => '50 mbytes',
		'filemark' => '4 kbytes'
	    ],
	},

	'dumptypes' => { },

	'interfaces' => { },

	'holdingdisks' => { },

	'dles' => [ ],
    };
    bless($self, $class);
    return $self;
}

=item C<add_param($param, $value)>

Add the given parameter to the configuration file, overriding any
previous value.  Note that strings which should be quoted in the configuration
file itself must be double-quoted here, e.g.,

  $testconf->add_param('org' => '"MyOrganization"');

=cut

sub add_param {
    my $self = shift;
    my ($param, $value) = @_;

    push @{$self->{'params'}}, $param, $value;
}

=item C<add_tapetype($name, $values_arrayref)>
=item C<add_dumptype($name, $values_arrayref)>
=item C<addholdingdisk($name, $values_arrayref)>
=item C<add_interface($name, $values_arrayref)>
=item C<add_application($name, $values_arrayref)>
=item C<add_script($name, $values_arrayref)>

Add the given subsection to the configuration file, including all
values in the arrayref.  The values should be specified as alternating
key/value pairs.

=cut

sub add_tapetype {
    my $self = shift;
    my ($name, $values_arrayref) = @_;
    $self->{'tapetypes'}{$name} = $values_arrayref;
}

sub add_dumptype {
    my $self = shift;
    my ($name, $values_arrayref) = @_;
    $self->{'dumptypes'}{$name} = $values_arrayref;
}

sub add_holdingdisk {
    my $self = shift;
    my ($name, $values_arrayref) = @_;
    $self->{'holdingdisks'}{$name} = $values_arrayref;
}

sub add_interface {
    my $self = shift;
    my ($name, $values_arrayref) = @_;
    $self->{'interfaces'}{$name} = $values_arrayref;
}

sub add_application {
    my $self = shift;
    my ($name, $values_arrayref) = @_;
    $self->{'application-tool'}{$name} = $values_arrayref;
}

sub add_script {
    my $self = shift;
    my ($name, $values_arrayref) = @_;
    $self->{'script-tool'}{$name} = $values_arrayref;
}

=item C<add_dle($line)>

Add a disklist entry; C<$line> is inserted verbatim into the disklist.

=cut

sub add_dle {
    my $self = shift;
    my ($line) = @_;
    push @{$self->{'dles'}}, $line;
}

=item C<write()>

Write out the accumulated configuration file, along with any other
files necessary to run Amanda.

=cut

sub write {
    my $self = shift;

    my $testconf_dir = "$CONFIG_DIR/TESTCONF";
    if (-e $testconf_dir) {
	rmtree($testconf_dir) or die("Could not remove '$testconf_dir'");
    }
    mkpath($testconf_dir);

    # set up curinfo dir, etc.
    mkpath($self->{'infofile'}) or die("Could not create infofile directory");
    mkpath($self->{'logdir'}) or die("Could not create logdir directory");
    mkpath($self->{'indexdir'}) or die("Could not create indexdir directory");
    my $amandates = $AMANDA_TMPDIR . "/TESTCONF/amandates";
    my $gnutar_listdir = $AMANDA_TMPDIR . "/TESTCONF/gnutar_listdir";
    if (! -d $gnutar_listdir) {
	mkpath($gnutar_listdir)
	    or die("Could not create '$gnutar_listdir'");
    }

    $self->_write_tapelist("$testconf_dir/tapelist");
    $self->_write_disklist("$testconf_dir/disklist");
    $self->_write_amanda_conf("$testconf_dir/amanda.conf");
    $self->_write_amandates($amandates);
    $self->_write_amanda_client_conf("$CONFIG_DIR/amanda-client.conf", $amandates, $gnutar_listdir);
}

sub _write_tapelist {
    my $self = shift;
    my ($filename) = @_;

    # create an empty tapelist
    open(my $tapelist, ">", $filename);
    close($tapelist);
}

sub _write_disklist {
    my $self = shift;
    my ($filename) = @_;

    # don't bother writing a disklist if there are no dle's
    return unless $self->{'dles'};

    open(my $disklist, ">", $filename);

    for my $dle_line (@{$self->{'dles'}}) {
	print $disklist "$dle_line\n";
    }

    close($disklist);
}

sub _write_amanda_conf {
    my $self = shift;
    my ($filename) = @_;

    open my $amanda_conf, ">", $filename
	or croak("Could not open '$filename'");

    # write key/value pairs
    my @params = @{$self->{'params'}};
    while (@params) {
	$param = shift @params;
	$value = shift @params;
	print $amanda_conf "$param $value\n";
    }

    # write out subsections
    $self->_write_amanda_conf_subsection($amanda_conf, "tapetype", $self->{"tapetypes"});
    $self->_write_amanda_conf_subsection($amanda_conf, "application-tool", $self->{"application-tool"});
    $self->_write_amanda_conf_subsection($amanda_conf, "script-tool", $self->{"script-tool"});
    $self->_write_amanda_conf_subsection($amanda_conf, "dumptype", $self->{"dumptypes"});
    $self->_write_amanda_conf_subsection($amanda_conf, "interface", $self->{"interfaces"});
    $self->_write_amanda_conf_subsection($amanda_conf, "holdingdisk", $self->{"holdingdisks"});

    close($amanda_conf);
}

sub _write_amanda_conf_subsection {
    my $self = shift;
    my ($amanda_conf, $subsec_type, $subsec_ref) = @_;

    for my $subsec_name (keys %$subsec_ref) {
	my @values = @{$subsec_ref->{$subsec_name}};
	
	if ($subsec_type eq "holdingdisk") {
	    print $amanda_conf "\nholdingdisk $subsec_name {\n";
	} else {
	    print $amanda_conf "\ndefine $subsec_type $subsec_name {\n";
	}

	while (@values) {
	    $param = shift @values;
	    $value = shift @values;
	    print $amanda_conf "$param $value\n";
	}
	print $amanda_conf "}\n";
    }
}

sub _write_amandates {
    my $self = shift;
    my ($filename) = @_;

    # make sure the containing directory exists
    mkpath($filename =~ /(^.*)\/amandates/);

    # truncate the file to eliminate any interference from previous runs
    open(my $amandates, ">", $filename) or die("Could not write to '$filename'");
    close($amandates);
}

sub _write_amanda_client_conf {
    my $self = shift;
    my ($filename, $amandates, $gnutar_listdir) = @_;

    # just an empty file for now
    open(my $amanda_client_conf, ">", $filename) or die("Could not write to '$filename'");
    print $amanda_client_conf "amandates \"$amandates\"\n";
    print $amanda_client_conf "gnutar_list_dir \"$gnutar_listdir\"\n";
    close($amanda_client_conf);
}

1;
