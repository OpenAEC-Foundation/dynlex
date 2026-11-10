// Real SDK/provider contract against private Auth and Firestore emulators.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {resolve} from 'node:path';
import {createFirebaseServices} from '../../site/platform/firebase.js';

const require=createRequire(resolve(process.env.DYNLEX_FIREBASE_TEST_DEPS,'package.json'));
const appAPI=require('firebase/app'),authAPI=require('firebase/auth'),storeAPI=require('firebase/firestore');
assert(process.env.FIRESTORE_EMULATOR_HOST);assert(process.env.FIREBASE_AUTH_EMULATOR_HOST);
const [storeHost,storePort]=process.env.FIRESTORE_EMULATOR_HOST.split(':');
const configuration={apiKey:'demo-key',projectId:'demo-dynlex-games',authDomain:'demo-dynlex-games.firebaseapp.com',appId:'demo-app'};
let auth,store;
const provider=createFirebaseServices({load:async()=>({
  app:appAPI,
  auth:{...authAPI,getAuth(app) {
    auth=authAPI.getAuth(app);authAPI.connectAuthEmulator(auth,'http://'+process.env.FIREBASE_AUTH_EMULATOR_HOST,{disableWarnings:true});return auth;
  }},
  store:{...storeAPI,getFirestore(app) {
    store=storeAPI.getFirestore(app);storeAPI.connectFirestoreEmulator(store,storeHost,Number(storePort));return store;
  }}
})});
const notifications=[],controller=new AbortController(),context={signal:controller.signal,emit:value=>notifications.push(value)};
const call=payload=>provider.services.firebase(payload,context);
const waitFor=async predicate=>{
  for(let attempt=0;attempt<200;attempt++) {
    if(predicate()) return;
    await new Promise(resolve=>setTimeout(resolve,25));
  }
  assert.fail('The emulator did not publish the expected auth identity');
};
const signIn=subject=>authAPI.signInWithCredential(auth,authAPI.GoogleAuthProvider.credential(JSON.stringify({sub:subject,email:subject+'@example.test',email_verified:true})));
let watched;
try {
  await call({operation:'connect',configuration});
  watched=call({operation:'watch'});
  await waitFor(()=>notifications.at(-1)?.user===null);
  await signIn('dynlex-provider-alice');
  const alice=auth.currentUser.uid;
  await waitFor(()=>notifications.at(-1)?.user?.uid===alice);
  const address={path:['players',alice,'games','progress'],field:'payload'};
  await storeAPI.deleteDoc(storeAPI.doc(store,...address.path));
  assert.deepEqual(await call({operation:'read',...address}),{value:null});
  const first=JSON.stringify({version:7,games:{},lastGame:null});
  const write=(expected,payload)=>({operation:'compare-set',...address,expected,data:{version:7,payload},timestampField:'updatedAt'});
  assert.deepEqual(await call(write(null,first)),{matched:true});
  assert.deepEqual(await call({operation:'read',...address}),{value:first});
  const second=first+' ',third=first+'\n';
  const raced=await Promise.all([call(write(first,second)),call(write(first,third))]);
  assert.deepEqual(raced.map(result=>result.matched).sort(),[false,true]);
  const saved=await call({operation:'read',...address});
  assert([second,third].includes(saved.value));
  assert.deepEqual(await call(write(first,'must not replace a newer save')),{matched:false});
  assert.deepEqual(await call({operation:'read',...address}),saved);
  await assert.rejects(call({...write(saved.value,first),data:{version:999,payload:first}}),error=>error.code==='permission-denied');
  await call({operation:'sign-out'});
  await waitFor(()=>notifications.at(-1)?.user===null);
  await assert.rejects(call({operation:'read',...address}),error=>error.code==='permission-denied');
  await signIn('dynlex-provider-bob');
  await waitFor(()=>notifications.at(-1)?.user?.uid===auth.currentUser.uid);
  await assert.rejects(call({operation:'read',...address}),error=>error.code==='permission-denied');
  await assert.rejects(call(write(saved.value,first)),error=>error.code==='permission-denied');
  const before=notifications.length;
  controller.abort();await watched;
  await authAPI.signOut(auth);await new Promise(resolve=>setTimeout(resolve,50));
  assert.equal(notifications.length,before,'Cancelled observers still emitted identities');
  console.log('Real Firebase provider: Google identities, owner isolation, guest denial, atomic competing writes, exact revisions, schema rules, sign-out and observer cancellation pass.');
} finally {
  controller.abort();if(watched) await watched;
  await provider.close();
}
