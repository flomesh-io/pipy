Two release media provided, rpm for RHEL7/Centos7 and docker image base on alpine. 
Download thru these links :
Piped rpm: http://repo.flomesh.cn/piped/piped-0.9.0-12.el7_pl.x86_64.rpm
Piped image: http://repo.flomesh.cn/images/piped-alpine-0.9.0-12.tar.gz

For rpm installation:
* yum -y localinstall piped-0.9.0-12.el7_pl.x86_64.rpm
* systemctl start/stop piped
* Default configuration files from /etc/piped/config.ini
* Alternatively, can run it by 'piped <config-file>'. There are some sample configrations in /etc/piped/test/* for reference
* Such as run a proxy like this : piped /etc/piped/test/proxy/config.ini

More doc will comming soon on flomesh.io 
