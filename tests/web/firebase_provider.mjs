import assert from 'node:assert/strict';
import {createFirebaseServices} from '../../site/platform/firebase.js';

function fixture() {
  const events=[],documents=new Map(),subscribers=new Set(),errors=[];
  let loads=0,deleted=0,ready=Promise.resolve(),readError=null,signInError=null,transactionAttempts=1;
  const user={uid:'alice'},auth={authStateReady:()=>ready};
  const snapshot=key=>({exists:()=>documents.has(key),get:name=>documents.get(key)?.[name]});
  const sdk={
    app:{initializeApp:configuration=>{events.push(['initialize',configuration]);return {name:'app'};},deleteApp:async()=>{deleted++;}},
    auth:{getAuth:()=>auth,GoogleAuthProvider:class {},
      onAuthStateChanged:(instance,listener)=>{assert.equal(instance,auth);subscribers.add(listener);queueMicrotask(()=>listener(null));return ()=>subscribers.delete(listener);},
      signInWithPopup:async()=>{if(signInError) throw signInError;for(const listener of subscribers) listener(user);},
      signOut:async()=>{for(const listener of subscribers) listener(null);}},
    store:{getFirestore:()=>({}),doc:(store,...parts)=>parts.join('/'),serverTimestamp:()=>({serverTime:true}),
      getDocFromServer:async key=>{if(readError) throw readError;return snapshot(key);},
      runTransaction:async(store,run)=>{
        let result;
        for(let i=0;i<transactionAttempts;i++) {
          const writes=[];
          result=await run({get:async key=>snapshot(key),set:(key,value)=>writes.push([key,value])});
          if(i===transactionAttempts-1) for(const [key,value] of writes) {documents.set(key,value);events.push(['write',key,value]);}
        }
        return result;
      }}
  };
  const provider=createFirebaseServices({load:async()=>{loads++;return sdk;},report:(operation,error)=>errors.push({operation,error})});
  const signal=new AbortController().signal;
  const call=(payload,context={signal,emit:()=>{throw new Error('Unexpected event');}})=>provider.services.firebase(payload,context);
  return {provider,call,documents,events,subscribers,errors,get loads(){return loads;},get deleted(){return deleted;},
    set ready(value){ready=value;},set readError(value){readError=value;},set signInError(value){signInError=value;},set attempts(value){transactionAttempts=value;}};
}
const f=fixture();assert.equal(f.loads,0,'Creating an unconfigured site loaded the SDK');
await assert.rejects(f.call({operation:'read'}),/not connected/);
await f.call({operation:'connect',configuration:{projectId:'test'}});assert.equal(f.loads,1);
await assert.rejects(f.call({operation:'connect',configuration:{}}),/already connected/);
const controller=new AbortController(),notifications=[];
const watched=f.call({operation:'watch'},{signal:controller.signal,emit:value=>notifications.push(value)});
await Promise.resolve();assert.deepEqual(notifications,[{type:'firebase-auth',user:null}]);
assert.deepEqual(await f.call({operation:'sign-in'}),{code:null});
assert.deepEqual(notifications.at(-1),{type:'firebase-auth',user:{uid:'alice'}});
const request={path:['players','alice','games','progress'],field:'payload'};
assert.deepEqual(await f.call({operation:'read',...request}),{value:null});
const write={operation:'compare-set',...request,expected:null,data:{version:7,payload:'first'},timestampField:'updatedAt'};
f.attempts=2;assert.deepEqual(await f.call(write),{matched:true});
assert.deepEqual(f.documents.get(request.path.join('/')),{version:7,payload:'first',updatedAt:{serverTime:true}});
assert.equal(f.events.filter(event=>event[0]==='write').length,1,'A retried transaction mutated application state');
assert.deepEqual(await f.call({...write,data:{version:7,payload:'stale'}}),{matched:false});
assert.deepEqual(await f.call({operation:'read',...request}),{value:'first'});
assert.deepEqual(await f.call({...write,expected:'first',data:{version:7,payload:'second'}}),{matched:true});
assert.deepEqual(await f.call({operation:'read',...request}),{value:'second'});
f.signInError=Object.assign(new Error('private provider detail'),{code:'auth/popup-closed-by-user'});
assert.deepEqual(await f.call({operation:'sign-in'}),{code:'auth/popup-closed-by-user'});
assert.equal(f.errors.at(-1).error.message,'private provider detail');
f.signInError=new Error('unexpected SDK failure');
await assert.rejects(f.call({operation:'sign-in'}),/unexpected SDK failure/);
f.readError=new Error('server unavailable');await assert.rejects(f.call({operation:'read',...request}),/server unavailable/);
await f.call({operation:'sign-out'});assert.deepEqual(notifications.at(-1),{type:'firebase-auth',user:null});
controller.abort();await watched;assert.equal(f.subscribers.size,0);
await f.provider.close();assert.equal(f.deleted,1);
await assert.rejects(f.call({operation:'read'}),{name:'AbortError'});

const failed=fixture();failed.ready=Promise.reject(new Error('initial auth failed'));
await assert.rejects(failed.call({operation:'connect',configuration:{}}),/initial auth failed/);
assert.equal(failed.deleted,1,'A failed initialization retained its app');
failed.ready=Promise.resolve();await failed.call({operation:'connect',configuration:{}});await failed.provider.close();assert.equal(failed.deleted,2);
const late=fixture();let finish;late.ready=new Promise(resolve=>{finish=resolve;});
const initializing=late.call({operation:'connect',configuration:{}});await Promise.resolve();
const closing=late.provider.close();finish();await assert.rejects(initializing,{name:'AbortError'});await closing;assert.equal(late.deleted,1);
const watchedClose=fixture();await watchedClose.call({operation:'connect',configuration:{}});
const pending=watchedClose.call({operation:'watch'},{signal:new AbortController().signal,emit:()=>{}});
await watchedClose.provider.close();await pending;assert.equal(watchedClose.subscribers.size,0);
console.log('Firebase primitives watch auth, compare-and-set atomically, preserve provider errors and clean up cancelled, failed and late lifetimes.');
