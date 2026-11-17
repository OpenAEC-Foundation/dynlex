// Address-ordered first fit with logarithmic lookup, splitting and coalescing.
// Each AVL subtree records its largest available contiguous range.
const height=node=>node===null?0:node.height;
const maximum=node=>node===null?0:node.maximum;
function refresh(node) {
  node.height=1+Math.max(height(node.left),height(node.right));
  node.maximum=Math.max(node.size,maximum(node.left),maximum(node.right));
  return node;
}
function rotateLeft(node) {
  const top=node.right;node.right=top.left;top.left=refresh(node);
  return refresh(top);
}
function rotateRight(node) {
  const top=node.left;node.left=top.right;top.right=refresh(node);
  return refresh(top);
}
function balance(node) {
  refresh(node);
  const tilt=height(node.left)-height(node.right);
  if(tilt>1) {
    if(height(node.left.left)<height(node.left.right)) node.left=rotateLeft(node.left);
    return rotateRight(node);
  }
  if(tilt< -1) {
    if(height(node.right.right)<height(node.right.left)) node.right=rotateRight(node.right);
    return rotateLeft(node);
  }
  return node;
}
function insert(node,added) {
  if(node===null) return added;
  if(added.pointer<node.pointer) node.left=insert(node.left,added);
  else if(added.pointer>node.pointer) node.right=insert(node.right,added);
  else throw new Error('Runtime free ranges overlap');
  return balance(node);
}
function erase(node,pointer) {
  if(node===null) throw new Error('Runtime free range is missing');
  if(pointer<node.pointer) node.left=erase(node.left,pointer);
  else if(pointer>node.pointer) node.right=erase(node.right,pointer);
  else {
    if(node.left===null) return node.right;
    if(node.right===null) return node.left;
    let next=node.right;while(next.left!==null) next=next.left;
    node.pointer=next.pointer;node.size=next.size;
    node.right=erase(node.right,next.pointer);
  }
  return balance(node);
}
function takeFirst(node,size,result) {
  if(node.left!==null&&node.left.maximum>=size) node.left=takeFirst(node.left,size,result);
  else if(node.size>=size) {
    result.pointer=node.pointer;
    if(node.size===size) return erase(node,node.pointer);
    // The remaining suffix stays between the same neighbours in address order.
    node.pointer+=size;node.size-=size;
  } else node.right=takeFirst(node.right,size,result);
  return balance(node);
}
function replaceRange(node,key,pointer,size) {
  if(node===null) throw new Error('Runtime free range is missing');
  if(key<node.pointer) node.left=replaceRange(node.left,key,pointer,size);
  else if(key>node.pointer) node.right=replaceRange(node.right,key,pointer,size);
  else {node.pointer=pointer;node.size=size;}
  return refresh(node);
}
export function createFreeRangeIndex() {
  let root=null;
  const taken={pointer:0};
  return {
    take(size) {
      if(root===null||root.maximum<size) return 0;
      root=takeFirst(root,size,taken);
      return taken.pointer;
    },
    release(pointer,size) {
      let node=root,previous=null,next=null;
      while(node!==null) {
        if(pointer<node.pointer) {next=node;node=node.left;}
        else {previous=node;node=node.right;}
      }
      if((previous!==null&&previous.pointer+previous.size>pointer)||
         (next!==null&&pointer+size>next.pointer)) throw new Error('Runtime free ranges overlap');
      const joinsPrevious=previous!==null&&previous.pointer+previous.size===pointer;
      const joinsNext=next!==null&&pointer+size===next.pointer;
      if(joinsPrevious) {
        const address=previous.pointer;
        size+=previous.size;
        if(joinsNext) {
          const nextAddress=next.pointer;
          size+=next.size;root=erase(root,nextAddress);
        }
        root=replaceRange(root,address,address,size);
      } else if(joinsNext) {
        const address=next.pointer;
        size+=next.size;
        root=replaceRange(root,address,pointer,size);
      } else root=insert(root,{pointer,size,maximum:size,height:1,left:null,right:null});
    }
  };
}
