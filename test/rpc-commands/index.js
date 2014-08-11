var syscoin = require('node-syscoin'),
	should = require('should'),
	async = require('async');

	// all config options are optional
var clients = [
	new syscoin.Client({
	    host: 'localhost',
	    port: 18368,
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

function syscoinready() {
	console.log('syscoin clients ready.');
}

var okCount = 0, hasError = false;
for(var i = 0;i < clients.length; i++) {
	clients[i].getInfo(function(err, results){
		if(err) {
			console.log('an error occurred: ' + JSON.stringify(err, null, 4));
			return hasError = true;
		}
 		console.log(JSON.stringify(results, null, 4));
		if(++okCount == clients.length) 
			syscoinready();
	});
}
