from pwn import *
import re

# Start the process
p = process('./chal')

# Receive initial prompt
print(p.recvuntil(b'leave a note:').decode())

# Define what you want to send
payload = b'A' * 8

# Send the payload
p.sendline(payload)

# Read the response
response = p.recvuntil(b'leave another note:')
print(f"Received: {response}")

# --- 1. PARSE THE RESPONSE ---
# Use regex to extract only the hex characters following "receipt: 0x"
match = re.search(b'receipt: 0x([0-9a-fA-F]+)', response)
if not match:
    print("[-] Could not find the receipt in the response.")
    exit(1)

extracted_hex = match.group(1).decode()
print(f"[*] Extracted Hex: {extracted_hex}")

# --- 2. CONVERT TO BYTES ---
# Convert the 16-character hex string into 8 raw bytes
receipt_bytes = bytes.fromhex(extracted_hex)

# --- 3. PERFORM XOR ---
# XOR the payload with the actual receipt bytes
xored_result = bytes([b1 ^ b2 for b1, b2 in zip(payload, receipt_bytes)])

print(f"[*] XOR Result (Hex): {xored_result.hex()}")
print(f"[*] XOR Result (Bytes): {xored_result}")

# --- 4. VERIFY ---
verification_receipt = bytes([b1 ^ b2 for b1, b2 in zip(payload, xored_result)])

if verification_receipt == receipt_bytes:
    print("[+] Verification successful: Payload ^ Key == Receipt")
else:
    print("[-] Verification failed.")


p.interactive()