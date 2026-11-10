// Test-only SDK injection; account policy and provider operations are production code.
import {createFirebaseServices as create} from './firebase.js';
export function createFirebaseServices() {
  return create({load:async()=>{
    const [app,auth,store]=await Promise.all([
      import('https://www.gstatic.com/firebasejs/12.18.0/firebase-app.js'),
      import('https://www.gstatic.com/firebasejs/12.18.0/firebase-auth.js'),
      import('https://www.gstatic.com/firebasejs/12.18.0/firebase-firestore.js')
    ]);
    window.accountTest={authAPI:auth,storeAPI:store,subject:'dynlex-browser-alice'};
    return {app,auth:{...auth,getAuth(app) {
      const instance=auth.getAuth(app);
      auth.connectAuthEmulator(instance,'http://127.0.0.1:8900',{disableWarnings:true});
      window.accountTest.auth=instance;return instance;
    },signInWithPopup(instance) {
      const subject=window.accountTest.subject;
      return auth.signInWithCredential(instance,auth.GoogleAuthProvider.credential(JSON.stringify({sub:subject,email:subject+'@example.test',email_verified:true})));
    }},store:{...store,getFirestore(app) {
      const instance=store.getFirestore(app);store.connectFirestoreEmulator(instance,'127.0.0.1',8899);
      window.accountTest.store=instance;return instance;
    }}};
  }});
}
