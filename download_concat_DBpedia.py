import os
import requests
import bz2
import shutil

# URL of the directory containing the files
base_url = "https://downloads.dbpedia.org/3.9/en/"

# Directory to save the downloaded and decompressed files
output_dir = "downloaded_files"
os.makedirs(output_dir, exist_ok=True)

# Function to download a file
def download_file(url, local_filename):
    with requests.get(url, stream=True) as r:
        r.raise_for_status()
        with open(local_filename, 'wb') as f:
            for chunk in r.iter_content(chunk_size=8192):
                f.write(chunk)
    return local_filename

# Function to decompress a .bz2 file
def decompress_file(bz2_file, output_file):
    # If output_file already exists, skip decompression
    if os.path.exists(output_file):
        print(f"File {output_file} already exists. Skipping decompression.")
        return
    with bz2.BZ2File(bz2_file, 'rb') as f_in:
        with open(output_file, 'wb') as f_out:
            shutil.copyfileobj(f_in, f_out)

# Function to concatenate all .nt files into a single file
def concatenate_files(file_list, output_file):
    with open(output_file, 'wb') as outfile:
        for fname in file_list:
            with open(fname, 'rb') as infile:
                shutil.copyfileobj(infile, outfile)

# Get the list of files from the directory
response = requests.get(base_url)
if response.status_code != 200:
    raise Exception(f"Failed to fetch directory listing from {base_url}")

# Parse the HTML to find all .nt.bz2 files
from bs4 import BeautifulSoup
soup = BeautifulSoup(response.text, 'html.parser')
files_to_download = [a['href'] for a in soup.find_all('a', href=True) if a['href'].endswith('.nt.bz2')]
print(len(files_to_download))

# Download and decompress each file
decompressed_files = []
for file_name in files_to_download:
    file_url = base_url + file_name
    bz2_file_path = os.path.join(output_dir, file_name)
    nt_file_path = os.path.join(output_dir, file_name.replace('.bz2', ''))

    print(f"Downloading {file_url}...")
    download_file(file_url, bz2_file_path)

    print(f"Decompressing {bz2_file_path}...")
    decompress_file(bz2_file_path, nt_file_path)

    decompressed_files.append(nt_file_path)

    # Optionally, remove the .bz2 file after decompression
    # os.remove(bz2_file_path)

# Concatenate all .nt files into a single file
final_output_file = os.path.join(output_dir, "dbpedia.nt")
print(f"Concatenating all .nt files into {final_output_file}...")
concatenate_files(decompressed_files, final_output_file)

print("Process completed successfully!")