#!/bin/bash

# Output CSV file name
OUTPUT_FILE="simulation_results.csv"

# List of trace files to process
TRACEFILES=(
  "tracefiles/fft_1024_p1.trf"     
  "tracefiles/fft_1024_p1-O2.trf"
  "tracefiles/matrix_mult_50_50_p1.trf"
  "tracefiles/matrix_mult_50_50_p1-O2.trf"
  "tracefiles/matrix_vector_8_5000_p1.trf"
  "tracefiles/matrix_vector_8_5000_p1-O2.trf"
  "tracefiles/matrix_vector_200_200_p1.trf"
  "tracefiles/matrix_vector_200_200_p1-O2.trf"
  "tracefiles/matrix_vector_5000_8_p1.trf"
  "tracefiles/matrix_vector_5000_8_p1-O2.trf"
)

# Write the CSV header
echo "file_name,CPU,Reads,RHit,Rmiss,Writes,WHit,WMiss,RHitrate,WHitrate,Hitrate,SimTime" > "$OUTPUT_FILE"

# Loop through each file in the specified list
for file_path in "${TRACEFILES[@]}"; do
  # Extract the file name from the path without the .trf extension
  file_name=$(basename "$file_path" .trf)

  # Execute the command and capture its output
  output=$(./assignment_1.bin 32768 0 "$file_path")

  # Check if the command produced valid output
  if [[ -z "$output" ]]; then
    echo "$file_name,ERROR: No output generated" >> "$OUTPUT_FILE"
    continue
  fi

  # Extract the required result line with a specific format
  result_line=$(echo "$output" | grep -E '^\s*0\s+[0-9]+\s+[0-9]+\s+[0-9]+\s+[0-9]+\s+[0-9]+\s+[0-9]+\s+[0-9]+(\.[0-9]+)?\s+[0-9]+(\.[0-9]+)?\s+[0-9]+(\.[0-9]+)?')
  sim_time=$(echo "$output" | tail -n 1 | awk '{print $(NF-1) $NF}')

  # Ensure the result line was found
  if [[ -z "$result_line" ]]; then
    echo "$file_name,ERROR: Result line not found" >> "$OUTPUT_FILE"
    continue
  fi

  # Convert the result line into comma-separated values
  formatted_values=$(echo "$result_line" | tr -s ' ' | sed 's/ /,/g')

  # Write the result to the CSV file
  echo "$file_name$formatted_values,$sim_time" >> "$OUTPUT_FILE"
done

# Print completion message
echo "Simulation data written to $OUTPUT_FILE"