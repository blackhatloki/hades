#!/usr/bin/perl
#
# Script to update the live routing tables. 
if ($ARGV[0] =~ /apply/){
   $doit=1;
}
$OS=`/usr/bin/facter operatingsystem`;
chomp $OS;
if ($OS =~ /SLES/){
  $prefix="ifroute";
  chdir("/etc/sysconfig/network");
}else{
  $prefix="route"; 
  chdir("/etc/sysconfig/network-scripts");
  open (NET,"</etc/sysconfig/network") or die "$!: could not open /etc/sysconfig/network";
  while (<NET>){
    chomp;
    s/\s+//g;
    if (/GATEWAY=/){ ($a,$gw)=split(/=/);} 
    if (/GATEWAYDEV=/){ ($a,$dev)=split(/=/);} 
  } 
  close NET;
  push (@routesconf,"default via $gw dev $dev");
}
#print "debug: @routesconf\n";
@routefiles=glob("$prefix-*");
if (scalar(@routefiles) < 1){
  print "No routes files found \n";
  exit 1;
}

foreach $routesfile (@routefiles){
  chomp $routesfile;
  ($a,$interface)=split(/\-/,$routesfile);
  open(RFILE,"<$routesfile") or die "$!: Could not open routesfile $routesfile";
  while (<RFILE>){
    if (!/^[0-9d]/){next;}
    s/0.0.0.0\/0/default/g;
    s/\s+$//g;
    s/ via//g;
    ($ip,$gw,$msk,$dev,$a,$src)=split(/\s+/);
    chomp $dev;
    chomp $src;
    $out="$ip via $gw dev $interface";
    if ($src){
       $out.=" src $src";
    }
    push (@routesconf,"$out"); 
  }
  close RFILE;
}
open(IPROUTE,"ip route|") or die "$!:Could not run ip route";
while(<IPROUTE>){
  next if (/scope/);
  next if (/^127.0/);
  s/\s+$//g;
  chomp;
  s/  / /g;
  push (@activeroutes,"$_");
}
close IPROUTE;

foreach $route (@routesconf){
  $found=0;
  foreach $route2 (@activeroutes){
#     print "comparing add |$route|$route2|$found\n";
     if ("$route" eq "$route2"){$found++;}
  }
  if ($found==0){ push(@routestoadd,"$route"); }
} 
foreach $route2 (@activeroutes){
  $found=0;
  foreach $route (@routesconf){
     if ("$route" eq "$route2"){$found++;}
#     print "comparing del |$route|$route2|$found\n";
  }
  if ($found==0){ push(@routestodel,"$route2"); }
}
foreach $route (@routestodel){
  print "ip route del $route\n";
  if ($doit==1){
    system ("ip route del $route");
  }
}
foreach $route (@routestoadd){
  print "ip route add $route\n";
  if ($doit==1){
    system ("ip route add $route");
  }
}
