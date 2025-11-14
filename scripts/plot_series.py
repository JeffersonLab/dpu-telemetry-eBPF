import json
import sys
import matplotlib.pyplot as plt
from matplotlib.ticker import MultipleLocator

if len(sys.argv) != 5:
    print("Usage: python plot_udp_second_line.py"
        " <file_200Hz> <file_2000Hz> <file_2000Hz> <file_4000Hz>")
    sys.exit(1)


def extract_udp_by_line(file_path, line_id):
    with open(file_path, "r") as f:
        lines = f.readlines()
        if len(lines) < 2:
            raise ValueError(f"{file_path} has fewer than 2 lines")
        line = lines[line_id].strip()
        data = json.loads(line)
        ts = next(iter(data))  # Only one timestamp per line
        ip_data = next(iter(data[ts].values()))
        udp_bytes = ip_data.get("udp_bytes")
        udp_packets = ip_data.get("udp_packets", [])
        print(f"{line_id}, {len(udp_packets)}, {sum(udp_packets)}, {min(udp_packets)}, {max(udp_packets)}")
        return udp_bytes, udp_packets

# Stats
for i in range(1, 5):  
    print(sys.argv[i]) 
    print("time_id, bins, sum_packets, min_packets, max_packets")
    for line_id in range(1, 20):
        extract_udp_by_line(sys.argv[i], line_id)
    print()


# Ploting area
bytes1, packets1 = extract_udp_by_line(sys.argv[1], 2)
bytes2, packets2 = extract_udp_by_line(sys.argv[2], 2)
bytes3, packets3 = extract_udp_by_line(sys.argv[3], 2)
bytes4, packets4 = extract_udp_by_line(sys.argv[4], 2)


x1 = list(range(len(bytes1)))
x2 = list(range(len(bytes2)))
x3 = list(range(len(bytes3)))
x4 = list(range(len(bytes4)))

# Plotting
plt.figure(figsize=(12, 6))

# Subplot 1
plt.subplot(4, 1, 1)
plt.plot(x1[:10], [p * 20 / 1000000.0 for p in packets1[5:15]])
plt.title("Run 1: polling at 20 Hz")
plt.xlabel("Time bins of a 0.5-second window")
plt.ylabel("Million PPS")
plt.grid(True)
plt.gca().xaxis.set_major_locator(MultipleLocator(1))  # Force step size = 1
plt.gca().yaxis.set_minor_locator(MultipleLocator(0.2))

# Subplot 2
plt.subplot(4, 1, 2)
plt.plot(x2[:100], [p * 200 / 1000000.0 for p in packets2[50:150]])
plt.title("Run 2: polling at 200 Hz")
plt.xlabel("Time bins of a 0.5-second window")
plt.ylabel("Million PPS")
plt.grid(True)
plt.gca().xaxis.set_major_locator(MultipleLocator(10))  # Force step size = 10
plt.gca().yaxis.set_minor_locator(MultipleLocator(0.2))

# Subplot 3
plt.subplot(4, 1, 3)
plt.plot(x3[:1000], [p * 2000 / 1000000.0 for p in packets3[500:1500]])
plt.title("Run 3: polling at 2000 Hz")
plt.xlabel("Time bins of a 0.5-second window")
plt.ylabel("Million PPS")
plt.grid(True)
plt.gca().xaxis.set_major_locator(MultipleLocator(100))  # Force step size = 100
plt.gca().yaxis.set_minor_locator(MultipleLocator(0.2))

# Subplot 4
plt.subplot(4, 1, 4)
plt.plot(x4[:2000], [p * 4000 / 1000000.0 for p in packets4[1000:3000]])
plt.title("Run 4: polling at 4000 Hz")
plt.xlabel("Time bins of a 0.5-second window")
plt.ylabel("Million PPS")
plt.grid(True)
plt.gca().xaxis.set_major_locator(MultipleLocator(200))  # Force step size = 200
plt.gca().yaxis.set_minor_locator(MultipleLocator(0.2))

plt.tight_layout()
plt.savefig("udp_plot.pdf", format="pdf")
print("Saved plot to udp_plot.pdf")
plt.show()
