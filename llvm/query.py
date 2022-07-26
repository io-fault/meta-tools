"""
# System queries for extracting usage information from `clang` and `llvm-config`.
"""
import sys
import itertools

from fault.system import execution
from fault.system import files

def split_config_output(flag, output):
	return set(map(str.strip, output.split(flag)))

def profile_library(prefix, architecture):
	profile_libs = [x for x in prefix.fs_iterfiles('data') if 'profile' in x.identifier]

	if len(profile_libs) == 1:
		# Presume target of interest.
		return profile_libs[0]
	else:
		# Scan for library with matching architecture.
		for x in profile_libs:
			if architecture in x.identifier:
				return x

	return None

def compiler_libraries(compiler, prefix, version, executable, target):
	"""
	# Attempt to select the compiler libraries directory containing compiler support
	# libraries for profiling, sanity, and runtime.
	"""
	lib = prefix + ['lib', 'clang', version, 'lib']
	syslib = lib / 'darwin' # Naturally, not always consistent.
	if syslib.fs_type() != 'void':
		return syslib
	syslib = prefix / 'lib' / 'darwin'
	if syslib.fs_type() != 'void':
		return syslib

def parse_clang_version_1(string):
	"""
	# clang --version parser.
	"""

	lines = string.split('\n')
	version_line = lines[0].strip()

	cctype, version_spec = version_line.split(' version ')
	try:
		version_info, release = version_spec.split('(', 1)
	except ValueError:
		# 9.0 from FreeBSD ports does not appear to have a "release" tag.
		version_info = version_spec
		release = ''

	release = release.strip('()')
	version = version_info.strip()
	version_info = version.split('.', 3)

	# Extract the default target from the compiler.
	target = None
	for line in lines:
		if line.startswith('Target:'):
			target = line
			target = target.split(':', 1)
			target = target[1].strip()
			break
	else:
		target = None

	return cctype, release, version, version_info, target

def parse_clang_directories_1(string):
	"""
	# Parse -print-search-dirs output.
	"""
	search_dirs_data = [x.split(':', 1) for x in string.split('\n') if x]

	return dict([
		(k.strip(' =:').lower(), list((x.strip(' =') for x in v.split(':'))))
		for k, v in search_dirs_data
	])

def parse_clang_standards_1(string):
	"""
	# After retrieving a list from standard error, extract as much information as possible.
	"""
	lines = string.split('\n')
	# first line should start with error:
	assert lines[0].strip().startswith('error:')

	return {
		x.split("'", 3)[1]: x.rsplit("'", 3)[2]
		for x in map(str.strip, lines[1:])
		if x.strip()
	}

def clang(executable, type='executable', libdir='lib'):
	"""
	# Extract information from the given clang &executable.
	"""
	warnings = []
	root = files.Path.from_absolute('/')
	cc_route = files.Path.from_absolute(executable)

	# gather compiler information.
	x = execution.prepare(type, executable, ['--version'])
	i = execution.KInvocation(*x)
	pid, exitcode, data = execution.dereference(i)
	data = data.decode('utf-8')
	cctype, release, version, version_info, target = parse_clang_version_1(data)

	# Analyze the library search directories.
	# Primarily interested in finding the crt*.o files for linkage.
	x = execution.prepare(type, executable, ['-print-search-dirs'])
	i = execution.KInvocation(*x)
	pid, exitcode, sdd = execution.dereference(i)
	search_dirs_data = parse_clang_directories_1(sdd.decode('utf-8'))

	ccprefix = files.Path.from_absolute(search_dirs_data['programs'][0])

	if target is None:
		warnings.append(('target', 'no target field available from --version output'))
		arch = None
	else:
		# First field of the target string.
		arch = target[:target.find('-')]

	cclib = compiler_libraries('clang', ccprefix, '.'.join(version_info), cc_route, target)
	builtins = None
	if cclib is None:
		cclib = files.Path.from_relative(root, search_dirs_data['libraries'][0])
		cclib = cclib / libdir / sys.platform

	if sys.platform in {'darwin'}:
		builtins = cclib / 'libclang_rt.osx.a'
	else:
		cclibs = [x for x in cclib.fs_iterfiles('data') if 'builtins' in x.identifier]

		if len(cclibs) == 1:
			builtins = str(cclibs[0])
		else:
			# Scan for library with matching architecture.
			for x, a in itertools.product(cclibs, [arch]):
				if a in x.identifier:
					builtins = str(x)
					break
			else:
				# clang, but no libclang_rt.
				builtins = None

	libdirs = [
		files.Path.from_relative(root, str(x).strip('/'))
		for x in search_dirs_data['libraries']
	]

	standards = {}
	for l in ('c', 'c++'):
		x = execution.prepare(type, executable, [
			'-x', l, '-std=void.abczyx.1', '-c', '/dev/null', '-o', '/dev/null',
		])
		pid, exitcode, stderr = execution.effect(execution.KInvocation(*x))
		standards[l] = parse_clang_standards_1(stderr.decode('utf-8'))

	clang = {
		'implementation': cctype.strip().replace(' ', '-').lower(),
		'libraries': str(cclib),
		'version': tuple(map(int, version_info)),
		'release': release,
		'command': str(cc_route),
		'runtime': str(builtins) if builtins else None,
		'standards': standards,
	}

	return clang

def instrumentation(llvm_config_path, merge_path=None, export_path=None, type='executable'):
	"""
	# Identify instrumentation related commands and libraries for making extraction tools.
	"""
	srcpath = str(llvm_config_path)
	v_pipe = ['--version']
	libs_pipe = ['profiledata', '--libs']
	syslibs_pipe = ['profiledata', '--system-libs']
	covlibs_pipe = ['coverage', '--libs']
	incs_pipe = ['--includedir']
	libdir_pipe = ['--libdir']
	rtti_pipe = ['--has-rtti']

	po = lambda x: execution.dereference(execution.KInvocation(*execution.prepare(type, srcpath, x)))
	outs = [
		po([srcpath, '--prefix']),
		po(v_pipe),
		po(libs_pipe),
		po(syslibs_pipe),
		po(covlibs_pipe),
		po(libdir_pipe),
		po(incs_pipe),
		po(rtti_pipe),
	]

	prefix, v, libs, syslibs, covlibs, libdirs, incdirs, rtti = [x[-1].decode('utf-8') for x in outs]

	libs = split_config_output('-l', libs)
	libs.discard('')
	libs.add('c++')

	covlibs = split_config_output('-l', covlibs)
	covlibs.discard('')

	syslibs = split_config_output('-l', syslibs)
	syslibs.discard('')
	syslibs.add('c++')

	libdirs = split_config_output('-L', libdirs)
	libdirs.discard('')
	dir, *reset = libdirs

	incdirs = split_config_output('-I', incdirs)
	incdirs.discard('')

	if rtti.lower() in {'yes', 'true', 'on'}:
		rtti = True
	else:
		rtti = False

	if not merge_path:
		merge_path = llvm_config_path.container/'llvm-profdata'
	if not export_path:
		export_path = llvm_config_path.container/'llvm-cov'

	fp = {
		'merge-command': str(merge_path),
		'include': incdirs,
		'library-directories': libdirs,
		'coverage-libraries': covlibs,
		'system-libraries': syslibs,
	}

	return v.strip(), srcpath, str(merge_path), str(export_path), fp

if __name__ == '__main__':
	import pprint
	pprint.pprint(clang(sys.argv[1]))
	pprint.pprint(list(instrumentation(files.Path.from_absolute(sys.argv[2]))))
