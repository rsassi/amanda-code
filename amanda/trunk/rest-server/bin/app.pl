#!/usr/bin/env perl

use lib '/amanda/h1/linux/lib/amanda/perl';

#Dancer::ModuleLoader->load_with_params('JSON', '-support_by_pp', '-convert_blessed_universally');

use Dancer;

use Encode::Locale;
use Encode;

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
use Amanda::Message;
use Amanda::Paths;
use Amanda::Process;
use Amanda::Util qw( :constants );
use Amanda::Rest::Amcheck;
use Amanda::Rest::Changers;
use Amanda::Rest::Configs;
use Amanda::Rest::Dles;
use Amanda::Rest::Dumps;
use Amanda::Rest::Labels;
use Amanda::Rest::Report;
use Amanda::Rest::Runs;
use Amanda::Rest::Services;
use Amanda::Rest::Storages::Labels;
use Amanda::Rest::Status;
use Amanda::Rest::Storages;
use Amanda::Rest::Version;

setting log_path => "/tmp/amanda/amanda-rest-server-log";
mkdir "/tmp/amanda/amanda-rest-server-log";

Amanda::Util::setup_application("amrest-server", "server", $CONTEXT_CMDLINE, "rest-server", "amanda");

set serializer => 'JSON';
set confdir => '/amanda/h1/linux/lib/amanda/rest-server/config-dancer';
#prepare_serializer_for_format;

get '/amanda/v1.0' => sub {
	my %p = params;
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Version::version(%p);
	status $status if $status > 0;
	return $r;
};

get '/amanda/v1.0/configs' => sub {
	my %p = params;
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Configs::list(%p);
	status $status if $status > 0;
	return $r
};

get '/amanda/v1.0/configs/:CONF' => sub {
	my %p = params;
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r)  = Amanda::Rest::Configs::fields(%p);
	status $status if $status > 0;
	return $r
};

get '/amanda/v1.0/configs/:CONF/storages' => sub {
	my %p = params;
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Storages::list(%p);
	status $status if $status > 0;
	return $r
};
get '/amanda/v1.0/configs/:CONF/storages/:STORAGE' => sub {
	my %p = params;
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Storages::fields(%p);
	status $status if $status > 0;
	return $r
};
post '/amanda/v1.0/configs/:CONF/storages/:STORAGE/inventory' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Storages::inventory(%p);
	status $status if $status > 0;
	return $r
};
post '/amanda/v1.0/configs/:CONF/storages/:STORAGE/show' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Storages::show(%p);
	return $r
};
post '/amanda/v1.0/configs/:CONF/storages/:STORAGE/reset' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Storages::reset(%p);
	status $status if $status > 0;
	return $r
};
post '/amanda/v1.0/configs/:CONF/storages/:STORAGE/update' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Storages::update(%p);
	status $status if $status > 0;
	return $r
};
post '/amanda/v1.0/configs/:CONF/storages/:STORAGE/eject' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Storages::eject(%p);
	status $status if $status > 0;
	return $r
};
post '/amanda/v1.0/configs/:CONF/storages/:STORAGE/clean' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Storages::clean(%p);
	status $status if $status > 0;
	return $r
};
post '/amanda/v1.0/configs/:CONF/storages/:STORAGE/create' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Storages::create(%p);
	status $status if $status > 0;
	return $r
};
post '/amanda/v1.0/configs/:CONF/storages/:STORAGE/verify' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Storages::verify(%p);
	status $status if $status > 0;
	return $r
};
post '/amanda/v1.0/configs/:CONF/storages/:STORAGE/load' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Storages::load(%p);
	status $status if $status > 0;
	return $r
};
post '/amanda/v1.0/configs/:CONF/storages/:STORAGE/label' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Storages::label(%p);
	status $status if $status > 0;
	return $r
};
get '/amanda/v1.0/configs/:CONF/storages/:STORAGE/labels' => sub {
	my %p = params;
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Storages::Labels::list(%p);
	status $status if $status > 0;
	return $r
};
get '/amanda/v1.0/configs/:CONF/storages/:STORAGE/labels/:LABEL' => sub {
	my %p = params;
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Storages::Labels::list(%p);
	status $status if $status > 0;
	return $r
};
post '/amanda/v1.0/configs/:CONF/storages/:STORAGE/labels' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Storages::Labels::add_label(%p);
	status $status if $status > 0;
	return $r
};
post '/amanda/v1.0/configs/:CONF/storages/:STORAGE/labels/:LABEL' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Storages::Labels::update_label(%p);
	status $status if $status > 0;
	return $r
};
del '/amanda/v1.0/configs/:CONF/storages/:STORAGE/labels' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Storages::Labels::erase(%p);
	status $status if $status > 0;
	return $r
};
del '/amanda/v1.0/configs/:CONF/storages/:STORAGE/labels/:LABEL' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Storages::Labels::erase(%p);
	status $status if $status > 0;
	return $r
};
get '/amanda/v1.0/configs/:CONF/labels' => sub {
	my %p = params;
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Labels::list(%p);
	status $status if $status > 0;
	return $r
};

#get '/amanda/v1.0/configs/:CONF/dles/hosts/:HOST' => sub {
#};
#get '/amanda/v1.0/configs/:CONF/dles/hosts/:HOST/disks/:DISK' => sub {
#};
post '/amanda/v1.0/configs/:CONF/dles/hosts/:HOST' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Dles::setting(%p);
	status $status if $status > 0;
	return $r
};
post '/amanda/v1.0/configs/:CONF/dles/hosts/:HOST/disks/:DISK' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Dles::setting(%p);
	status $status if $status > 0;
	return $r
};
get '/amanda/v1.0/configs/:CONF/dles/hosts/:HOST/estimate' => sub {
	my %p = params;
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Dles::estimate(%p);
	status $status if $status > 0;
	return $r
};
get '/amanda/v1.0/configs/:CONF/dles/hosts/:HOST' => sub {
	my %p = params;
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Dles::info(%p);
	status $status if $status > 0;
	return $r
};
get '/amanda/v1.0/configs/:CONF/dles/hosts/:HOST/due' => sub {
	my %p = params;
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Dles::due(%p);
	status $status if $status > 0;
	return $r
};

get '/amanda/v1.0/configs/:CONF/dumps' => sub {
	my %p = params;
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Dumps::list(%p);
	status $status if $status > 0;
	return $r
};
get '/amanda/v1.0/configs/:CONF/dumps/hosts/:HOST' => sub {
	my %p = params;
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Dumps::list(%p);
	status $status if $status > 0;
	return $r
};
get '/amanda/v1.0/configs/:CONF/dumps/hosts/:HOST/disks/:DISK' => sub {
	my %p = params;
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Dumps::list(%p);
	status $status if $status > 0;
	return $r
};

get '/amanda/v1.0/configs/:CONF/status' => sub {
	my %p = params;
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Status::current(%p);
	status $status if $status > 0;
	return $r
};
get '/amanda/v1.0/configs/:CONF/report' => sub {
	my %p = params;
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Report::report(%p);
	status $status if $status > 0;
	return $r
};

get '/amanda/v1.0/configs/:CONF/runs' => sub {
	my %p = params;
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Runs::list(%p);
	status $status if $status > 0;
	return $r
};
post '/amanda/v1.0/configs/:CONF/runs/amdump' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Runs::amdump(%p);
	status $status if $status > 0;
	return $r
};
post '/amanda/v1.0/configs/:CONF/runs/amflush' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Runs::amflush(%p);
	status $status if $status > 0;
	return $r
};
post '/amanda/v1.0/configs/:CONF/runs/amvault' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Runs::amvault(%p);
	status $status if $status > 0;
	return $r
};

post '/amanda/v1.0/configs/:CONF/runs/checkdump' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Runs::checkdump(%p);
	status $status if $status > 0;
	return $r;
};

post '/amanda/v1.0/configs/:CONF/runs/fetchdump' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Runs::fetchdump(%p);
	status $status if $status > 0;
	return $r;
};

get '/amanda/v1.0/configs/:CONF/runs/messages' => sub {
	my %p = params;
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Runs::messages(%p);
	status $status if $status > 0;
	return $r;
};
del '/amanda/v1.0/configs/:CONF/runs' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Runs::kill(%p);
	status $status if $status > 0;
	return $r;
};
post '/amanda/v1.0/configs/:CONF/amcheck' => sub {
	my %options;
	my %p;
	if (request->body) {
	    my $rp = from_json(request->body, \%options);
	    %p = (params, %$rp);
	} else {
	    %p = params;
	}
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Amcheck::check(%p);
	status $status if $status > 0;
	return $r;
};

get '/amanda/v1.0/services/discover' => sub {
	my %p = params;
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Services::discover(%p);
	status $status if $status > 0;
	return $r;
};

get '/amanda/v1.0/configs/:CONF/changers' => sub {
	my %p = params;
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Changers::list(%p);
	status $status if $status > 0;
	return $r
};
get '/amanda/v1.0/configs/:CONF/changers/:CHANGER' => sub {
	my %p = params;
	Amanda::Message::_apply(sub { $_[0] = encode(locale => $_[0]); }, {}, %p);
	my ($status, $r) = Amanda::Rest::Changers::fields(%p);
	status $status if $status > 0;
	return $r
};

my $extensions_dir = $Amanda::Constants::REST_EXTENSIONS_DIR;

if (-d $extensions_dir) {
    foreach my $file (<$extensions_dir/*>) {
        eval { require $file };
	if ($@) {
	    die($@);
	}

    }
}

dance;
