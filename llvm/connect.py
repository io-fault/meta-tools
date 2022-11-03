"""
# Instantiate the `fault-llvm` tools project into a target directory.
"""
import os.path

from fault.system import files
from fault.system import process
from fault.system.factors import context as factors

from fault.project import system as lsf
from fault.project import factory

from . import query

formats = {
	'http://if.fault.io/factors/system': [
		('elements', 'cc', '2014', 'c++'),
		('elements', 'c', '2011', 'c'),
		('void', 'h', 'header', 'c'),
		('references', 'sr', 'lines', 'text'),
	],
	'http://if.fault.io/factors/python': [
		('module', 'py', 'psf-v3', 'python'),
		('interface', 'pyi', 'psf-v3', 'python'),
	],
	'http://if.fault.io/factors/meta': [
		('references', 'fr', 'lines', 'text'),
	],
}

info = lsf.types.Information(
	identifier = 'http://fault.io/development/tools//llvm',
	name = 'fault-llvm-adapters',
	authority = 'fault.io',
	contact = "http://fault.io/critical"
)

fr = lsf.types.factor@'meta.references'
sr = lsf.types.factor@'system.references'

def declare(ipq, deline):
	includes, = ipq['include']
	includes = files.root@includes
	libdirs = sorted(list(ipq['library-directories']))

	soles = [
		('fault', fr, '\n'.join([
			'http://fault.io/integration/machines/include',
		])),
		('libclang-is', sr, '\n'.join(
			libdirs + ['clang', ''],
		)),
		('libllvm-is', sr, '\n'.join(
			libdirs + \
			sorted(list(ipq['coverage-libraries'])) + \
			sorted(list(ipq['system-libraries'])) + ['']
		)),
	]

	sets = [
		('libclang-if',
			'http://if.fault.io/factors/meta.sources', (), [
				('clang-c', (includes/'clang-c')),
			]),
		('libllvm-if',
			'http://if.fault.io/factors/meta.sources', (), [
				('llvm', (includes/'llvm')),
				('llvm-c', (includes/'llvm-c')),
			]),

		('delineate',
			'http://if.fault.io/factors/system.executable',
			['.fault', '.libclang-is', '.libclang-if'], [
				(x.identifier, x) for x in deline
			]),
		('ipquery',
			'http://if.fault.io/factors/system.executable',
			['.fault', '.libllvm-is', '.libllvm-if'], [
				('ipq.cc', ipq['source']),
			]),
	]

	return factory.Parameters.define(info, formats, sets=sets, soles=soles)

def main(inv:process.Invocation) -> process.Exit:
	target, llvmconfig = inv.args
	route = files.Path.from_path(os.path.realpath(target))

	# Identify ipq.cc, delineate.c, and json.c
	factors.load()
	factors.configure()
	pd, pj, fp = factors.split(__name__)
	llvm_d = fp.container
	llvm_factors = {k[0]: v[1] for k, v in pj.select(llvm_d)}

	# Get the libraries and interfaces needed out of &query
	v, src, merge, export, ipqd = query.instrumentation(files.root@llvmconfig)
	ipqd['source'] = llvm_factors[llvm_d/'ipq'][0][1]

	# Sources of the image factors.
	deline = (
		llvm_factors[llvm_d/'delineate'][0][1],
		llvm_factors[llvm_d/'json'][0][1],
	)

	p = declare(ipqd, deline)
	factory.instantiate(p, route)
	return inv.exit(0)
