# PQC_IoT

This project integrates `wolfSSL` with the ESP32 platform to evaluate Post-Quantum Cryptography (PQC) in IoT applications. It was created as part of a master's thesis focused on evaluating the prospects of using PQC on resource-constrained IoT devices.

The project focuses on standardized algorithms such as ML-KEM and ML-DSA and analyzes their performance, resource usage, and impact on secure communication using TLS 1.3 and MQTT.


## Project Measurement Summary

### Implemented files
- **main_server_tls.c**: [main/main_server_tls.c](main/main_server_tls.c) - TLS 1.3 handshake implementation between MQTT client and broker with broker authentication. Uses post-quantum KEM and certificates;
- **main_client_tls.c**: [main/main_client_tls.c](main/main_client_tls.c) - TLS 1.3 handshake implementation between MQTT client and broker with broker and client authentication. Uses post-quantum KEM and certificates; 
- **main_kem.c**: [main/main_kem.c](main/main_kem.c) - Implementation of the main ML-KEM operations with measurement wrapping;
- **main_dsa.c**: [main/main_dsa.c](main/main_dsa.c) - Implementation of the main ML-DSA operations with measurement wrapping;
- **measure.c**: [components/measure/measure.c](components/measure/measure.c) - Measurement utility for marking the start and end of measurements, recording results, and exporting them to CSV format.
- **energy.py**: [energy.py](energy.py) - A script used to calculate the energy consumption of operations recorded from oscilloscope voltage waveforms.

### Files used for measurement purposes
- **tls13.c**: [components/wolfssl__wolfssl/src/tls13.c](components/wolfssl__wolfssl/src/tls13.c) - Core TLS 1.3 implementation modified for measurement integration.
- **mqtt_socket.c**: [components/wolfssl__wolfmqtt/src/mqtt_socket.c](components/wolfssl__wolfmqtt/src/mqtt_socket.c) - MQTT socket TLS wrappers modified for measurement integration.

### Program design
- **How to get results**: The measurement utility collects CPU clock cycles, time, stack and heap usage and exports the obtained results via the export_csv() function in measure.c.
- **Where to look**:  Start and stop measurement hooks are placed throughout the `main` examples and inside modified component files (tls13.c, mqtt_socket.c).
- **How are energy calculations performed**: The script detects voltage peaks in a CSV file, subtracts the baseline level, calculates the energy of each detected peak using the shunt resistor value and supply voltage, and saves the results to a CSV file. The script also generates a PDF plot showing the detected peaks, baseline, and threshold.





