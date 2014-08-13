var syscoin = require('node-syscoin'),
	should = require('should'),
	async = require('async');

	// all config options are optional
var clients = [
	new syscoin.Client({
	    host: 'localhost',
	    port: 28368,
	    user: 'u',
	    pass: 'p',
	    timeout: 180000
	}),
		new syscoin.Client({
	    host: 'localhost',
	    port: 19008,
	    user: 'u',
	    pass: 'p',
	    timeout: 180000
	}),
	new syscoin.Client({
	    host: 'localhost',
	    port: 19018,
	    user: 'u',
	    pass: 'p',
	    timeout: 180000
	}),
	new syscoin.Client({
	    host: 'localhost',
	    port: 19028,
	    user: 'u',
	    pass: 'p',
	    timeout: 180000
	})
];

function Syscoin() {
	this.clients = clients;
}
Syscoin.prototype.setGenerate = function(cindexes, valu, amt, callback) {
	for(var n=0;n<cindexes.length;n++) {
		var client = this.clients[cindexes[n]],
			pcount = 0;
		client.setGenerate(valu, amt, function(err, ok) {
			if(err) return callback(err);
			if(++pcount===cindexes.length)
				return callback(null, true);
		});
	}
};
Syscoin.prototype.getInfo = function(cindexes, callback) {
	for(var n=0;n<cindexes.length;n++) {
		var client = this.clients[cindexes[n]],
			pcount = 0,
			retval = [];
		client.getInfo(function(err, ok) {
			if(err) return callback(err);
			retval.push(ok);
			if(++pcount===cindexes.length)
				return callback(null, retval);
		});
	}
};
Syscoin.prototype.registerAlias = function(cindex, alias, value, callback) {
	var client = this.clients[cindex],
		retval = [],
		cbcnt = 0;
	var confirm = function() {
		client.aliasInfo(alias, 
			function(err, info) {
				if(err) {
					if(++cbcnt >= 25) return callback(err);
					else setTimeout(confirm, 10000);
				}
				else {
					retval.push(info);
					callback(null, retval);
				}
			});		
	};
  	var activate = function() {
		var data = retval[0];
		client.aliasActivate(alias, data[1], value, 
			function(err, ok) {
				if(err) return callback(err);
				else {
					console.log('aliasactivate ' + alias 
						+ ' ' + data[1] 
						+ ' ' + value 
						+ '\n' + JSON.stringify(ok,null,4));
					retval.push(ok);
					setTimeout(confirm, 100);
				}
			});
	};
	client.aliasNew(alias, 
		function(err, ok) {
			if(err) return callback(err);
			else {
				console.log('aliasnew ' + alias 
					+ '\n' + JSON.stringify(ok,null,4));
				retval.push(ok);
				setTimeout(activate, 100);
			}
		});
}

var _s;

function syscoinready() {
	_s = new Syscoin();
	console.log('syscoin clients ready.');
	// _s.setGenerate([0],false,-1,function(err,ok){
	// 	if(ok)console.log('ok, did that');
	// });
	// _s.getInfo([0],function(err,info){
	// 	if(info)console.log(JSON.stringify(info, null, 4));
	// });
	_s.registerAlias(0,'assgaper', 'gape that ass', function(err,info){
		if(err) console.log(JSON.stringify(err, null, 4));
		if(info) console.log(JSON.stringify(info, null, 4));
	});
}

var okCount = 0, hasError = false;
for(var i = 0;i < clients.length; i++) {
	clients[i].getInfo(function(err, results){
		if(err) {
			console.log('an error occurred: ' + JSON.stringify(err, null, 4));
			return hasError = true;
		}
 		console.log('online. balance ' + results.balance);
		if(++okCount == clients.length) 
			syscoinready();
	});
}
