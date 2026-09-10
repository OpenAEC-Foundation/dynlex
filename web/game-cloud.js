import { readProgress } from "./game-progress.js";

export async function connectGameCloud(config) {
  const [{initializeApp},authAPI,dbAPI] = await Promise.all([
    import("https://www.gstatic.com/firebasejs/12.18.0/firebase-app.js"),
    import("https://www.gstatic.com/firebasejs/12.18.0/firebase-auth.js"),
    import("https://www.gstatic.com/firebasejs/12.18.0/firebase-firestore.js")
  ]);
  const app = initializeApp(config);
  const auth = authAPI.getAuth(app), db = dbAPI.getFirestore(app);
  await auth.authStateReady();
  const reference = uid => dbAPI.doc(db,"players",uid,"games","progress");
  const revisions = new Map();
  return {
    get user() { return auth.currentUser; },
    signIn() { return authAPI.signInWithPopup(auth,new authAPI.GoogleAuthProvider()); },
    signOut() { return authAPI.signOut(auth); },
    onAuth(listener) { return authAPI.onAuthStateChanged(auth,listener); },
    async read(uid) {
      const snapshot = await dbAPI.getDocFromServer(reference(uid));
      const payload = snapshot.exists() ? snapshot.data().payload : null;
      revisions.set(uid,payload);
      return payload === null ? null : readProgress(payload);
    },
    async write(uid,progress) {
      // A transaction detects another device's edits before overwriting them.
      await dbAPI.runTransaction(db,async transaction => {
        const ref=reference(uid),snapshot=await transaction.get(ref);
        const payload=snapshot.exists() ? snapshot.data().payload : null;
        if(payload !== revisions.get(uid)) {
          const error=new Error("Another device changed this save");error.code="save-conflict";throw error;
        }
        transaction.set(ref,{version:progress.version,payload:JSON.stringify(progress),updatedAt:dbAPI.serverTimestamp()});
      });
      revisions.set(uid,JSON.stringify(progress));
    }
  };
}
