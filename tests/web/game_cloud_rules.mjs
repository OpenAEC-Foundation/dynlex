import { createRequire } from "node:module";
import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import { emptyProgress, SAVE_VERSION } from "../../web/game-progress.js";
const require=createRequire(resolve(process.env.DYNLEX_FIREBASE_TEST_DEPS,'package.json'));
const {initializeTestEnvironment,assertSucceeds,assertFails}=require('@firebase/rules-unit-testing');
const {doc,setDoc,getDoc,serverTimestamp}=require('firebase/firestore');
const env=await initializeTestEnvironment({projectId:'demo-dynlex-games',firestore:{host:'127.0.0.1',port:8899,rules:await readFile(new URL('../../firestore.rules',import.meta.url),'utf8')}});
try {
 const alice=env.authenticatedContext('alice').firestore();
 const bob=env.authenticatedContext('bob').firestore();
 const guest=env.unauthenticatedContext().firestore();
 const path=['players','alice','games','progress'];
 const save={version:SAVE_VERSION,payload:JSON.stringify(emptyProgress()),updatedAt:serverTimestamp()};
 await assertSucceeds(setDoc(doc(alice,...path),save));
 await assertSucceeds(getDoc(doc(alice,...path)));
 await assertFails(getDoc(doc(bob,...path)));
 await assertFails(setDoc(doc(bob,...path),save));
 await assertFails(getDoc(doc(guest,...path)));
 await assertFails(setDoc(doc(guest,...path),save));
 await assertFails(setDoc(doc(alice,...path),{...save,version:999}));
 await assertFails(setDoc(doc(alice,...path),{...save,payload:'x'.repeat(900001)}));
 await assertFails(setDoc(doc(alice,...path),{...save,updatedAt:0}));
 console.log('Firestore emulator: owners can save and load; other users, guests and invalid documents are denied.');
} finally {await env.cleanup();}
