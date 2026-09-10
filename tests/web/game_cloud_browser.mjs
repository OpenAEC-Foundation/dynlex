import assert from "node:assert/strict";
import {navigate,waitFor,evaluate,closeBrowserSession,runtimeExceptions} from "./browser_test_driver.mjs";
await navigate('/farm.css');
await waitFor("location.pathname==='/farm.css'",'cloud fixture');
const result=await evaluate(`(async()=>{
 const {initializeApp}=await import('https://www.gstatic.com/firebasejs/12.18.0/firebase-app.js');
 const authAPI=await import('https://www.gstatic.com/firebasejs/12.18.0/firebase-auth.js');
 const dbAPI=await import('https://www.gstatic.com/firebasejs/12.18.0/firebase-firestore.js');
 const config={apiKey:'demo-key',projectId:'demo-dynlex-games',authDomain:'demo-dynlex-games.firebaseapp.com',appId:'demo-app'};
 const app=initializeApp(config),auth=authAPI.getAuth(app),db=dbAPI.getFirestore(app);
 authAPI.connectAuthEmulator(auth,'http://127.0.0.1:8900',{disableWarnings:true});
 dbAPI.connectFirestoreEmulator(db,'127.0.0.1',8899);
 const signIn=sub=>authAPI.signInWithCredential(auth,authAPI.GoogleAuthProvider.credential(JSON.stringify({sub,email:sub+'@example.com',email_verified:true})));
 await signIn('alice');
 const {connectGameCloud}=await import('/game-cloud.js');
 const {emptyProgress,SAVE_VERSION}=await import('/game-progress.js');
 const {RIVER_GOALS,newGoalProgress}=await import('/coding-goals.js');
 const cloud=await connectGameCloud(config),uid=cloud.user.uid;
 const initial=await cloud.read(uid),save=emptyProgress();
 save.games.river={updatedAt:1,data:{source:'take the sheep',solved:false,coding:newGoalProgress(RIVER_GOALS)}};save.lastGame='river';
 await cloud.write(uid,save);
 const loaded=await cloud.read(uid);
 const other=structuredClone(save);other.games.river.updatedAt=2;
 await dbAPI.setDoc(dbAPI.doc(db,'players',uid,'games','progress'),{version:SAVE_VERSION,payload:JSON.stringify(other),updatedAt:dbAPI.serverTimestamp()});
 let conflict=null;
 try {await cloud.write(uid,save);} catch(error) {conflict=error.code;}
 await signIn('bob');let denied=null;
 try {await cloud.read(uid);} catch(error) {denied=error.code;}
 await cloud.signOut();
 return {initial,loaded,conflict,denied,signedOut:cloud.user===null};
})()`);
assert.equal(result.initial,null);
assert.equal(result.loaded.games.river.data.source,'take the sheep');
assert.equal(result.conflict,'save-conflict');
assert.equal(result.denied,'permission-denied');
assert.equal(result.signedOut,true);
assert.deepEqual(runtimeExceptions,[]);
console.log('Real browser Firebase SDK with emulated Google credentials: save/load, account isolation, conflict detection and sign-out passed.');
await closeBrowserSession();
