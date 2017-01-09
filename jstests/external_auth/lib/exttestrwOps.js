// note: run setup.js first with mongod --auth disabled
// then run this with mongod --auth enabled

// name: External user with readWrite access to 'test'
// mode: auth

function exttestrwOpsRun(){
  'use strict'
  var db = conn.getDB( '$external' )

  assert(
    db.auth({
      user: 'exttestrw',
      pwd: 'exttestrw9a5S',
      mechanism: 'PLAIN',
      digestPassword: false
    })
  )

  // check who we are authenticated as

  var res = db.runCommand({connectionStatus : 1})

  assert ( res.authInfo.authenticatedUsers[0].user == "exttestrw")

  // test access

  load( 'jstests/external_auth/lib/_functions.js' )

  authuser_assertrw( db.getSiblingDB('test') )
  authuser_assertnone( db.getSiblingDB('other') )
  authuser_assertnone( db.getSiblingDB('yetanother') )
}

exttestrwOpsRun()
