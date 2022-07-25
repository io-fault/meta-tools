"""
# Process delineated json to be free of trailing commas.

# The libclang delineation tool intentionally leaves trailing commas in order
# to simplify serialization procedures. However, JSON mandates their absence.

# In order for this tool to operate safely, commas present in JSON strings must be escaped.
"""

from fault.system import files
from fault.system import process

def main(inv:process.Invocation) -> process.Exit:
	target, root = map(files.Path.from_path, inv.argv)

	for d, ds in root.fs_index():
		if 'elements.json' not in (x.identifier for x in ds):
			# Not a unit of interest without elements.json.
			continue

		urpath = d.segment(root)
		(target + urpath).fs_alloc().fs_mkdir()

		for f in ds:
			rpath = f.segment(root)
			b = f.fs_load()
			b = b.replace(b',}', b'}')
			b = b.replace(b',]', b']')
			(target + rpath).fs_store(b)

	return inv.exit(0)

if __name__ == '__main__':
	process.control(main, process.Invocation.system())
