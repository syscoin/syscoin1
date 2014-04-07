#SYSCOIN COMMANDS


##alias commands - these commands deal with standard aliases.

**aliasnew** <alias>
Create a new alias
<alias>: alias name to reserve. max 255 chars.
**returns:** 
{ 
	aliastxid, 
	aliaskey
}
**example:** 
$ syscoind aliasnew /d/microsoft.bit
[
    "577b0f85da4744cbbe7ce763fe58bd4e3fc9817271fbc3785c6178217246ba94",
    "1150cd188f74c8df00"
]
 

**aliasactivate** <alias> <aliaskey> [<aliastxid>] <aliasval>
Activate the alias. may be called directly after aliasnew.  However, txn isn't actually posted to network until 120 blocks from aliasnew.
<alias>: alias name.
<aliaskey>: alias key from aliasnew result.  If client is restarted before aliasactivate is called, then aliaxtxid param is required.
<aliastxid>: alias txn id from aliasnew command.  Required if client is restarted.
<aliasval>: value to associate with alias.  Max 1023 chars.
**returns:** 
{ 
	aliastxid 
}
**example:**
$ syscoind aliasactivate /d/microsoft.bit 1150cd188f74c8df00 RESERVED
8aba3631593aeb622d07900bad4c30d91d79377a2dd8cd3e5db6e972cca54282


**aliasupdate** <alias> <value>
Update alias with new data.  Also resets expiration height.
<alias>: Alias to update. 
<value>: Value to associate with alias.  Max 1023 chars.
**returns:** 
{ 
	aliastxid 
}
**example:**
$ syscoind aliasupdate /d/microsoft.bit "{ns:['17.17.17.17','12.12.12.12']}"
0da9761fccd19a881ceb06a90a61386fa2104a726eba631eea0ef2084a281c00


**aliasinfo** <alias>
Show the values for the alias.
**returns:**
{
    name,
    value,
    txid,
    address,
    expires_in
}
**example:**
$ syscoind aliasinfo /d/microsoft.bit
{
    "name" : "/d/microsoft.bit",
    "value" : "{ns:['17.17.17.17','12.12.12.12']}",
    "txid" : "0da9761fccd19a881ceb06a90a61386fa2104a726eba631eea0ef2084a281c00",
    "address" : "3MFdW9PsX1s92qd5ZGurqeWU1xx8iW9zb3",
    "expires_in" : 153924
}



**aliashistory**

**aliasfilter**

**aliasscan**


##data alias commands
these commands deal with data aliases.

**datanew** <alias>
Create a new data alias
<alias>: alias name to reserve. max 255 chars.
**returns:** 
{ 
	aliastxid, 
	aliaskey
}
**example:**
$ syscoind datanew /dat/mycompany/readme
[
    "a42f21f1502fa6078b62cc6500f31815bbc97fb4f12a91e95e5f242d9d467728",
    "40738ea3fbd88d5c"
]


**dataactivate** <alias> <aliaskey> [<aliastxid>] <filename> <data>
Activate the alias. may be called directly after aliasnew.  However, txn isn't actually posted to network until 120 blocks from aliasnew.
<alias>: alias name.
<aliaskey>: alias key from aliasnew result.  If client is restarted before aliasactivate is called, then aliaxtxid param is required.
<aliastxid>: alias txn id from aliasnew command.  Required if client is restarted.
<filename>: filename to associate with data. Max 1023 chars.
<data>: value to associate with alias, base64-encoded.  Max 256kB.
**returns:**
{ 
	aliastxid 
}
**example:** 
$ syscoind dataactivate /dat/mycompany/readme 40738ea3fbd88d5c readme.txt VGhpcyBpcyBteSBjb21wYW55Cg==
ae07eedeb53b6103d8fa6e689b1466e4c2853f27f5cc389bef910c59eb041dc1


**dataupdate** <alias> <filename> <data>
Update alias with new data.  Also resets expiration height.
<alias>: Alias to update. 
<value>: Value to associate with alias.  Max 1023 chars.
**returns:** 
{ 
	aliastxid 
}
**example:** 
$ syscoind dataupdate /dat/mycompany/readme readme.txt V2VsY29tZSB0byBteSBjb21wYW55Cg==
82fa2fbbf640fd7ad002552399b1094d50861f7a9c5d5402fbe0ad4d14d3e907


**datainfo** <alias>
Show the values for the data alias.
**returns:**
{
    name,
    filename,
    txid,
    address,
    expires_in,
    data
}
**example:** 
$ syscoind datainfo /dat/mycompany/readme
{
    "name" : "/dat/mycompany/readme",
    "filename" : "readme.txt",
    "txid" : "ae07eedeb53b6103d8fa6e689b1466e4c2853f27f5cc389bef910c59eb041dc1",
    "address" : "3K6fZck7ksF6jXZcsfHE2LWbrjX4Knunpn",
    "expires_in" : 157872,
    "data" : "VGhpcyBpcyBteSBjb21wYW55Cg=="
}

**datahistory**

**datafilter**

**datascan**


##anonymous data commands

**setdata**

**dumpdata**


##offer commands
these commands deal with creating or purchasing offers.

**offernew** <category> <title> <quantity> <price> [<description>]
Create a new offer
<category>: offer category. max 255 chars.
<title>: offer title. max 255 chars.
<quantity>: item quantity available.
<price>: Price for item, in Syscoin.
<description>: offer description. max 16k chars.
**returns:** 
[
    offertxid, 
    offerkey
]
**example:**
$ syscoind offernew "General/Widgets" "Model T Widget Manifolds" 25 249 "One brand new model T widget manifold, still in original packaging."
[
    "7c6f5d153f2b8f7b1e38772c45370ab647335eea30ece99740ebeb3e43c18652",
    "8d0dabf5e7f2a4a300"
]

**offeractivate** <offerkey> [<offertxid>]
Activate the offer. may be called directly after offernew. Once active, an offer may be accepted (purchased) by calling offeraccept. Posted to network immediately after offernew.
<offerkey>: offer key.
<offertxid>: offer txn id from offernew command.  Required if client is restarted.
**returns:** 
    aliastxid 
**example:** 
$ syscoind offeractivate 8d0dabf5e7f2a4a300
913c83bb63998421f9425c21e2b15bf09a96ea8dcbc03722efc3d2f6ffb0e867

**offerupdate** <offerkey> <category> <title> <quantity> <price> [<description>]
Update an offer
<offerkey>: offer key.
<category>: offer category. max 255 chars.
<title>: offer title. max 255 chars.
<quantity>: item quantity available. NOTE performs a relative qty adjustment e.g. -5 subtracts 5 from qty
<price>: Price for item, in Satoshis.
<description>: offer description. max 16k chars.
**returns:** 
    offertxid
**example:**
$ syscoind offerupdate 8d0dabf5e7f2a4a300 "General/Widgets" "Model T Widget Manifolds" 10 249 "One brand new model T widget manifold, still in original packaging."
95d6385779e43a705c27cfc1347fa70e3583b06e357836cdf36059e7594da946


**offeraccept** <offerkey> [<quantity>]
Accept an offer
<offerkey>: offer key.
[<quantity>]: quantity to accept. optional, default 1.
**returns:** 
{ 
    accepttxid,
    acceptkey
}
**example:**
$ syscoind offeraccept 8d0dabf5e7f2a4a300 5
[
    "e229b2ef75807a4d5ceaa46b92677647de6363e45a5374670e500522bf1f2e71",
    "24579dbfc4b4c3eb00"
]

**offerpay** <acceptkey> [<accepttxid>] <acceptmessage>
Pay for an offer. Automatically sends seller payment in Syscoin, and also pays all syscoin service fees & marks the accept as 'paid'.
<acceptkey>: accept key.
<accepttxid>: accept txn id from offeraccept command.  Required if client is restarted.
<acceptmessage>: message to seller, accept-specific. A buyer shipping address goes here in many use-cases.
**returns:** 
{ 
    paytxid,
    offertxid
}
**example:**
$ syscoind offerpay 24579dbfc4b4c3eb00 "Please ship to: John Smith, 123 Main Street, Anytown CA 91111"
[
    "387f8e5ea2d21a4c6b679cf33d6dfd2366b0f43222811b21cc1441ed9e680ff7",
    "3d99e7650dfb60b377717ac4328f13bda92610a8e7076d3e065e231281f13d76"
]


**offerinfo**
Show the values for the offer.
**returns:**
{
    id,
    title,
    description,
    price,
    quantity,
    fees_paid,
    txn,
    height,
    accepts : []
}
**example:** 
$ syscoind offerinfo 8d0dabf5e7f2a4a300
{
    "id" : "8d0dabf5e7f2a4a300",
    "txid" : "3d99e7650dfb60b377717ac4328f13bda92610a8e7076d3e065e231281f13d76",
    "address" : "3F1282vJVPhVz5dxDJ5r3SPNy61kWDy7SF",
    "expires_in" : 111947,
    "payment_address" : "SbxUr7CVa4UYfxn1pkT1SNqQEAM3bzrYvX",
    "category" : "General/Widgets",
    "title" : "Model T Widget Manifolds",
    "quantity" : 30,
    "price" : 249.00000000,
    "fee" : 24.45000000,
    "description" : "One brand new model T widget manifold, still in original packaging.",
    "accepts" : [
        {
            "id" : "24579dbfc4b4c3eb00",
            "txid" : "3d99e7650dfb60b377717ac4328f13bda92610a8e7076d3e065e231281f13d76",
            "height" : "444",
            "time" : "1396853158",
            "quantity" : 5,
            "price" : 249.00000000,
            "paid" : "true",
            "fee" : 10.22500000,
            "paytxid" : "31215b19260474d41c0627778e2ccf5a3974551ca396e2a9bb9dea5209251061",
            "message" : "Please ship to: John Smith, 123 Main Street, Anytown CA 91111"
        }
    ]
}


**offerhistory**

**offerfilter**

**offerscan**


##certificate commands
these commands deal with issuing and transferring certificates.

**certissuernew** <title> <data>
<title>: certificate title. max 255 chars.
<data>: certificate data, max 64 kB
**returns**
[
    txid,
    certissuerkey
]
**example:**
$ syscoind certissuernew "Nerd Academy - We Teach you the Art of Nerd" "This certificate issuer is used to generate certificate of accreditations for our students. These are lifetime certificate and not honored if transferred or stolen."
[
    "76ac8fc00b06c722918e6702837077bb27cf56a384f12935f5b3d40c7b3efc62",
    "d9d333abf5d9714f"
]


**certissueractivate** <certissuerkey>
<certissuerkey> certificate issuer key generated from certissuernew command.
**returns**
    txid 
**example:**
syscoind certissueractivate d9d333abf5d9714f
2088c50c7de00c30ed6e6ffa0661b18ad148cf8b8dcc5927ae844e58e2b78678


**certissuerupdate** <certissuerkey> <title> <data>
<certissuerkey> certificate key
<title>: certificate title. max 255 chars.
<data>: certificate data, max 64 kB
**returns**
    txid
**example:**


**certnew** <certissuerkey> <destaddress> <title> <data>
<certissuerkey> certificate key
<destaddress> destination Syscoin address
<title>: certificate title. max 255 chars.
<data>: certificate data, max 64 kB
**returns**
[
    txid,
    certkey
]
**example:**
$ syscoind certnew d9d333abf5d9714f Scq4eMRi8aRZxJXWas5MW8uxYCtZZgkcT1 "Certificate of Nerd Master" "Owner has achieved Nerd Mastery."
[
    "6b79eaf9c171fb92a5873f86e0b1069dbd79359b0f0cce14c201fc8fd2863587",
    "d532408da773032a"
]

**certtransfer** <certkey> <destaddress>
<certkey>
<destaddress>
**returns**
**returns:**


**certissuerinfo** <certissuerkey>
**example:**
$ syscoind certissuerinfo d9d333abf5d9714f
{
    "id" : "d9d333abf5d9714f",
    "txid" : "6b79eaf9c171fb92a5873f86e0b1069dbd79359b0f0cce14c201fc8fd2863587",
    "address" : "33h59NnTq48hgrFUXYBrpL6pGhgmE8Fdjk",
    "expires_in" : 123029,
    "title" : "Nerd Academy - We Teach you the Art of Nerd",
    "data" : "This certificate issuer is used to generate certificate of accreditations for our students. These are lifetime certificate and not honored if transferred or stolen.",
    "certificates" : [
        {
            "id" : "d532408da773032a",
            "txid" : "6b79eaf9c171fb92a5873f86e0b1069dbd79359b0f0cce14c201fc8fd2863587",
            "height" : "483",
            "time" : "1396857128",
            "fee" : 0.00000000,
            "title" : "Certificate of Nerd Master",
            "data" : "Owner has achieved Nerd Mastery."
        }
    ]
}


**certinfo** <certkey>
**example:**
$ syscoind certinfo d532408da773032a
{
    "id" : "d532408da773032a",
    "txid" : "6b79eaf9c171fb92a5873f86e0b1069dbd79359b0f0cce14c201fc8fd2863587",
    "height" : "483",
    "time" : "1396857128",
    "fee" : 0.00000000,
    "title" : "Certificate of Nerd Master",
    "data" : "Owner has achieved Nerd Mastery.",
    "issuer" : {
        "id" : "d9d333abf5d9714f",
        "txid" : "6b79eaf9c171fb92a5873f86e0b1069dbd79359b0f0cce14c201fc8fd2863587",
        "address" : "33h59NnTq48hgrFUXYBrpL6pGhgmE8Fdjk",
        "expires_in" : 123029,
        "title" : "Nerd Academy - We Teach you the Art of Nerd",
        "data" : "This certificate issuer is used to generate certificate of accreditations for our students. These are lifetime certificate and not honored if transferred or stolen."
    }
}


**certissuerhistory**

**certissuerfilter**

**certissuerscan**
