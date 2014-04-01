#SYSCOIN COMMANDS


##alias commands - these commands deal with standard aliases.

**aliasnew** <alias>
Create a new alias
<alias>: alias name to reserve. max 255 chars.
returns: 
{ 
	aliastxid, 
	aliaskey
}


**aliasactivate** <alias> [<aliaskey> <aliastxid>] <aliasval>
Activate the alias. may be called directly after aliasnew.  However, txn isn't actually posted to network until 120 blocks from aliasnew.
<alias>: alias name.
<aliaskey>: alias key from aliasnew result.  If client is restarted before aliasactivate is called, then aliaxtxid param is required.
<aliastxid>: alias txn id from aliasnew command.  Required if client is restarted.
<aliasval>: value to associate with alias.  Max 1023 chars.
returns: 
{ 
	aliastxid 
}


**aliasupdate** <alias> <value>
Update alias with new data.  Also resets expiration height.
<alias>: Alias to update. 
<value>: Value to associate with alias.  Max 1023 chars.
returns: 
{ 
	aliastxid 
}


**aliasinfo** <alias>
Show the values for the alias.
returns:
{
    name,
    value,
    txid,
    address,
    expires_in
}

**aliashistory**

**aliasfilter**

**aliasscan**


##data alias commands
these commands deal with data aliases.

**datanew** <alias>
Create a new data alias
<alias>: alias name to reserve. max 255 chars.
returns: 
{ 
	aliastxid, 
	aliaskey
}

**dataactivate** <alias> [<aliaskey> <aliastxid>] <filename> <data>
Activate the alias. may be called directly after aliasnew.  However, txn isn't actually posted to network until 120 blocks from aliasnew.
<alias>: alias name.
<aliaskey>: alias key from aliasnew result.  If client is restarted before aliasactivate is called, then aliaxtxid param is required.
<aliastxid>: alias txn id from aliasnew command.  Required if client is restarted.
<filename>: filename to associate with data. Max 1023 chars.
<data>: value to associate with alias, base64-encoded.  Max 256kB.
returns: 
{ 
	aliastxid 
}

**dataupdate** <alias> <filename> <data>
Update alias with new data.  Also resets expiration height.
<alias>: Alias to update. 
<value>: Value to associate with alias.  Max 1023 chars.
returns: 
{ 
	aliastxid 
}


**datainfo** <alias>
Show the values for the data alias.
returns:
{
    name,
    value,
    txid,
    address,
    expires_in,
    filename,
    data
}

**datahistory**

**datafilter**

**datascan**


##anonymous data commands

**setdata**

**dumpdata**


##offer commands
these commands deal with creating or purchasing offers.

**offernew**
Create a new offer
<category>: offer category. max 255 chars.
<title>: offer title. max 255 chars.
<quantity>: item quantity available.
<price>: Price for item, in Satoshis.
<description>: offer description. max 16k chars.
returns: 
{ 
    offertxid, 
    offerkey
}

**offeractivate**
Activate the offer. may be called directly after offernew.  Posted to network immediately after offernew.
<offerkey>: offer key.
returns: 
{ 
    aliastxid 
}

**offerupdate**
Update an offer
<offerkey>: offer key.
<category>: offer category. max 255 chars.
<title>: offer title. max 255 chars.
<quantity>: item quantity available.
<price>: Price for item, in Satoshis.
<description>: offer description. max 16k chars.
returns: 
{ 
    offertxid
}

**offeraccept**
Accept an offer
<offerkey>: offer key.
[<quantity>]: quantity to accept. optional, default 1.
returns: 
{ 
    offertxid,
    acceptkey
}

**offerpay**
Pay for an offer
<acceptkey>: accept key.
returns: 
{ 
    paytxid,
    offertxid
}

**offerinfo**
Show the values for the offer.
returns:
{
    id,
    title,
    description,
    price,
    quantity,
    fees_paid,
    txn,
    height,
    accepts : [
        {
            
        }
    ]
}

**offerhistory**

**offerfilter**

**offerscan**


##certificate commands
these commands deal with issuing and transferring certificates.

**certissuernew**

**certissueractivate**

**certissuerupdate**

**certissue**

**certtransfer**

**certissuerinfo**

**certinfo**

**certissuerhistory**

**certissuerfilter**

**certissuerscan**
