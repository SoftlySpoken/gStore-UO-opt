from rdflib import Graph
import os

def convert_owl_to_turtle(input_dir, output_file):
    # Create an RDF graph
    g = Graph()

    # Iterate over all files in the input directory
    for filename in os.listdir(input_dir):
        if filename.endswith(".owl"):
            file_path = os.path.join(input_dir, filename)
            print(f"Parsing file: {file_path}")
            
            # Parse the OWL file into the graph
            g.parse(file_path, format="xml")

    # Serialize the graph to N-Triples format and save to the output file
    with open(output_file, "w", encoding="utf-8") as f:
        f.write(g.serialize(format="nt"))
    
    print(f"Turtle RDF data saved to: {output_file}")

# Example usage
input_directory = "lubm500m/"  # Replace with the path to your OWL files
output_turtle_file = "lubm500m.ttl"      # Replace with the desired output file name
convert_owl_to_turtle(input_directory, output_turtle_file)