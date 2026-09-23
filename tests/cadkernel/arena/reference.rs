// SPDX-License-Identifier: MPL-2.0
// Linked against the unmodified pinned src/brep/arena.rs.
extern crate cadkernel_arena_source;
use cadkernel_arena_source::{Arena, Key};
use std::hash::{Hash, Hasher};

#[derive(Default)]
struct Words(Vec<u32>);
impl Hasher for Words {
    fn finish(&self) -> u64 { panic!("the trace does not choose a hash algorithm") }
    fn write(&mut self, _: &[u8]) { panic!("Key must hash typed u32 words") }
    fn write_u32(&mut self, value: u32) { self.0.push(value); }
}
fn main() {
    let mut nodes: Arena<i32> = Arena::new();
    let mut keys: Vec<Key<i32>> = Vec::new();
    for step in 0..400 {
        let operation = (step * 17 + 5) % 7;
        println!("{operation}");
        if keys.is_empty() || operation <= 1 {
            let key = nodes.insert(step * 3 - 50);
            keys.push(key);
            println!("{key:?}");
        } else {
            let key = keys[step as usize % keys.len()];
            match operation {
                2 => {
                    let removed = nodes.remove(key);
                    println!("{}", removed.is_some());
                    if let Some(value) = removed { println!("{value}"); }
                },
                3 => {
                    let found = nodes.get(key);
                    println!("{}", found.is_some());
                    if let Some(value) = found { println!("{value}"); }
                },
                4 => {
                    let mut duplicate = nodes.clone();
                    if let Some(value) = duplicate.get_mut(key) { *value += 1000; }
                    println!("{:?}", duplicate.get(key));
                    println!("{:?}", nodes.get(key));
                },
                5 => {
                    println!("{}", nodes.iter().map(|(_, value)| *value).sum::<i32>());
                    println!("{nodes:?}");
                },
                6 => {
                    let mut words = Words::default();
                    key.hash(&mut words);
                    println!("{}", words.0[0]);
                    println!("{}", words.0[1]);
                    println!("{}", key == key.clone());
                },
                _ => unreachable!(),
            }
        }
        println!("{}", nodes.len());
        println!("{}", nodes.is_empty());
    }
    // Key<T>: Clone is unconditional, including when a key is itself a payload.
    let mut container: Arena<Key<i32>> = Arena::new();
    let key = container.insert(keys[0]);
    let cloned = container.clone();
    println!("{:?}", cloned.get(key).unwrap());
}
