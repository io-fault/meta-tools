"""
# Instantiate the `fault-llvm` tools project into a target directory.
"""

from fault.system import files
from fault.system import process

from fault.project import system as lsf
from fault.project import factory

infra = {
	'fault-c-interfaces': [
		lsf.types.Reference('http://fault.io/integration/machine',
			lsf.types.factor@'include'),
		lsf.types.Reference('http://fault.io/integration/python',
			lsf.types.factor@'include'),
	],
	'integration-llvm': [
		lsf.types.Reference('http://fault.io/integration/machine',
			lsf.types.factor@'llvm.tools'),
	],
	'*.c': [
		lsf.types.Reference('http://if.fault.io/factors',
			lsf.types.factor@'system', 'type', 'c.2011'),
	],
	'*.cc': [
		lsf.types.Reference('http://if.fault.io/factors',
			lsf.types.factor@'system', 'type', 'c++.2011'),
	],
	'*.h': [
		lsf.types.Reference('http://if.fault.io/factors',
			lsf.types.factor@'system', 'type', 'c.header'),
	],
	'*.pyi': [
		lsf.types.Reference('http://if.fault.io/factors',
			lsf.types.factor@'python.interface', 'type', 'python.psf-v3'),
	],
}

info = lsf.types.Information(
	identifier = 'http://fault.io/integration/machine//llvm',
	name = 'fault-llvm',
	authority = 'fault.io',
	abstract = "Tool adapter instance for LLVM.",
	icon = dict([('emoji', "🛠️")]),
	contact = "&<http://fault.io/critical>"
)

def declare():
	deline = "/* Delineation */\n"
	deline += "#include <fault/llvm/json.c>\n"
	deline += "#include <fault/llvm/delineate.c>\n"
	dsrcs = [
		('libclang-delineate.c', deline),
	]
	dsyms = [
		'fault-c-interfaces',
		'integration-llvm',
		# Administration Provision
		'libclang',
	]

	llvmcov = "/* Coverage Extraction */\n"
	llvmcov += "#include <coverage.cc>\n"
	pyi = "/* Python Interfaces */\n"
	pyi += "#include <python.c>\n"
	csrcs = [
		('python.c', pyi),
		('llvm.cc', llvmcov),
	]
	csyms = [
		'fault-c-interfaces',
		'integration-llvm',
		# Administration Provision
		'llvm-coverage',
	]

	soles = [
		('coverage', lsf.types.factor@'python.interface', "# Empty."),
	]
	sets = [
		('delineate',
			'http://if.fault.io/factors/system.executable', dsyms, dsrcs),
		('extensions.coverage',
			'http://if.fault.io/factors/system.extension', csyms, csrcs),
	]

	return factory.Parameters.define(info, infra.items(), sets=sets, soles=soles)

def main(inv:process.Invocation) -> process.Exit:
	target, = inv.args

	route = files.Path.from_path(target)
	p = declare()
	factory.instantiate(p, route)
	return inv.exit(0)
