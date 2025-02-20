import binascii
import struct
import matplotlib.pyplot as plt
import numpy as np
from cmsisdsp import arm_float_to_q15

def parse_ihex_line(line):
    if not line.startswith(":"):
        raise ValueError("Invalid Intel HEX line")
    
    line = line.strip()
    byte_count = int(line[1:3], 16)  # Number of data bytes
    address = int(line[3:7], 16)  # Load address
    record_type = int(line[7:9], 16)  # Record type
    data = line[9:9 + (byte_count * 2)]  # Data bytes in hex
    checksum = int(line[-2:], 16)  # Checksum
    
    # Convert hex string to bytes
    data_bytes = binascii.unhexlify(data)
    
    return {
        "byte_count": byte_count,
        "address": address,
        "record_type": record_type,
        "data_bytes": data_bytes,
        "checksum": checksum
    }

def extract_data_from_file(file_path):
    extracted_data = []
    with open(file_path, "r") as f:
        for line in f:
            try:
                parsed_data = parse_ihex_line(line)
                data_bytes = parsed_data["data_bytes"]
                
                # Split data into 2-byte chunks and convert to signed Q15 integers
                chunks = [struct.unpack("<h", data_bytes[i:i+2])[0] for i in range(0, len(data_bytes), 2)]
                extracted_data.extend(chunks)
            except ValueError as e:
                print(f"Skipping invalid line: {line.strip()} - {e}")
    return extracted_data

# Example usage
file_path = "C:\\Users\\K_frelih\\Documents\\akustika\\dump.hex"
data_q15_integers = extract_data_from_file(file_path)

# Convert to actual Q15 floating-point values
data_q15_floats = [x / 32768.0 for x in data_q15_integers]  # 2^15 = 32768

#float_signal = data_q15_integers.astype(np.float32)

#plot samples
plt.plot(data_q15_integers)
plt.xlabel('Sample Index')
plt.ylabel('Amplitude (Q15)')
plt.title('Q15 Signal Plot')
plt.show()

from cmsisdsp import arm_rfft_instance_q15, arm_rfft_init_q15, arm_rfft_q15, arm_cmplx_mag_q15,arm_vlog_q15,arm_q15_to_float
audio_samples_q15 = arm_float_to_q15(data_q15_floats)

# Initialize the FFT
rfft_instance_q15 = arm_rfft_instance_q15()
status = arm_rfft_init_q15(rfft_instance_q15, 512, 0, 1)

# Apply the FFT to the audio
rfft_1_q15 = arm_rfft_q15(rfft_instance_q15, audio_samples_q15)
xf = np.fft.rfftfreq(len(audio_samples_q15), d=1./44100)
# Take the absolute value
fft_bins_1_q15 = arm_cmplx_mag_q15(rfft_1_q15)[:512 // 2 + 1]
fft_bins_1_q15=arm_vlog_q15(fft_bins_1_q15)
plt.plot(fft_bins_1_q15)
plt.xlabel('Sample Index')
plt.ylabel('fft')
plt.title('FFT CMSIS')
plt.show()

fft_bins_1_q15_scaled = arm_q15_to_float(fft_bins_1_q15) * 512
fft_bins_1_q15_scaled=np.log10(np.abs(fft_bins_1_q15_scaled))
plt.plot(fft_bins_1_q15_scaled, label = "Rescaled q15 Values")
plt.ylabel('fft')
plt.title("scaled Q15.")
plt.legend()
plt.show()




# Compute FFT NP
fft = np.fft.rfft(data_q15_floats)
fft_bins = np.abs(fft)
plt.plot(fft_bins)
plt.xlabel('Sample Index')
plt.ylabel('fft')
plt.title('FFT')
plt.show()



