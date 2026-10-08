use ring::{pbkdf2, rand};
use ring::rand::SecureRandom;
use ring::aead::{Aad, LessSafeKey, Nonce, UnboundKey, AES_256_GCM};
use std::num::NonZeroU32;

const PBKDF2_ITERATIONS: u32 = 100_000;
const SALT_LEN: usize = 32;
const NONCE_LEN: usize = 12;

fn derive_key(salt: &[u8]) -> Vec<u8> {
    let seed1: &[u8] = b"vibi_core_2025";
    let seed2: &[u8] = b"session_log_key_v1";
    let mut combined = Vec::new();
    combined.extend_from_slice(seed1);
    combined.extend_from_slice(b":");
    combined.extend_from_slice(seed2);
    combined.extend_from_slice(b":");
    combined.extend_from_slice(b"internal_log_encrypt_key_x7k9m2p4q8r3t6w1");
    
    let mut key = vec![0u8; 32];
    pbkdf2::derive(
        pbkdf2::PBKDF2_HMAC_SHA512,
        NonZeroU32::new(PBKDF2_ITERATIONS).unwrap(),
        salt,
        &combined,
        &mut key,
    );
    key
}

pub fn encrypt_session_data(plaintext: &str) -> Result<String, String> {
    let rng = rand::SystemRandom::new();
    
    let mut salt = vec![0u8; SALT_LEN];
    rng.fill(&mut salt).map_err(|e| format!("RNG error: {:?}", e))?;
    
    let key_bytes = derive_key(&salt);
    let unbound_key = UnboundKey::new(&AES_256_GCM, &key_bytes)
        .map_err(|e| format!("Key error: {:?}", e))?;
    let key = LessSafeKey::new(unbound_key);
    
    let mut nonce_bytes = vec![0u8; NONCE_LEN];
    rng.fill(&mut nonce_bytes).map_err(|e| format!("RNG error: {:?}", e))?;
    let nonce = Nonce::assume_unique_for_key(nonce_bytes.clone().try_into().unwrap());
    
    let mut in_out = plaintext.as_bytes().to_vec();
    key.seal_in_place_append_tag(nonce, Aad::empty(), &mut in_out)
        .map_err(|e| format!("Encryption error: {:?}", e))?;
    
    let result = format!(
        "{}:{}:{}",
        hex_encode(&salt),
        hex_encode(&nonce_bytes),
        hex_encode(&in_out)
    );
    Ok(result)
}

pub fn decrypt_session_data(encrypted: &str) -> Result<String, String> {
    let parts: Vec<&str> = encrypted.split(':').collect();
    if parts.len() != 3 {
        return Err("Invalid format".to_string());
    }
    
    let salt = hex_decode(parts[0])?;
    let nonce_bytes = hex_decode(parts[1])?;
    let ciphertext = hex_decode(parts[2])?;
    
    if nonce_bytes.len() != NONCE_LEN {
        return Err("Invalid nonce length".to_string());
    }
    
    let key_bytes = derive_key(&salt);
    let unbound_key = UnboundKey::new(&AES_256_GCM, &key_bytes)
        .map_err(|e| format!("Key error: {:?}", e))?;
    let key = LessSafeKey::new(unbound_key);
    
    let nonce = Nonce::assume_unique_for_key(nonce_bytes.try_into().unwrap());
    
    let mut in_out = ciphertext;
    let plaintext = key.open_in_place(nonce, Aad::empty(), &mut in_out)
        .map_err(|_| "🔒 FILE TAMPERED — Cannot decrypt. This session log has been modified externally.".to_string())?;
    
    String::from_utf8(plaintext.to_vec()).map_err(|e| format!("UTF-8 error: {:?}", e))
}

fn hex_encode(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{:02x}", b)).collect()
}

fn hex_decode(hex: &str) -> Result<Vec<u8>, String> {
    if hex.len() % 2 != 0 {
        return Err("Invalid hex length".to_string());
    }
    (0..hex.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&hex[i..i+2], 16).map_err(|e| format!("Hex error: {:?}", e)))
        .collect()
}