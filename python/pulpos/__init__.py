#
# Copyright (C) 2025 2025 GreenWaves Technologies, ETH Zurich and University of Bologna
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

#
# Authors: Germain Haugou (germain.haugou@gmail.com)
#

import os
import sys
import importlib.util
import logging
import collections
import gvrun.commands
import gvrun.target
import dataclasses
import rich.tree
from pulpos.toolchain import Toolchain, _ToolchainCFlags, _ToolchainLdFlags

# Make sure we look for sources in every directory specified in PULPOS_MODULES
_modules = os.environ.get('PULPOS_MODULES')
if _modules is not None:
    pulpos_source_paths = _modules.split(':')


def get_home(container):
    """Returns pulpos home folder

    This is taken from PULPOS_HOME envvar. Raises an error if it is not defined.

    Returns
    -------
    str
        Pulpos home folder.
    """
    home = os.environ.get('PULPOS_HOME')
    if home is None:
        raise RuntimeError(f'{container._get_title(True)} PULPOS_HOME is nto defined')
    return home


def get_path_from_source(path: str):
    """ Returns the absolute path of the file

    The file specified by its file path is searched into the source code search paths
    and its absolute within one of the search path is returned.
    If the file is not found in the search paths, its path is returned as it is
    Parameters
    ----------
    path : str
        The path of the file relative to one of the source code search path
    """
    global pulpos_source_paths
    if os.path.isabs(path):
        return path

    for source_path in pulpos_source_paths:
        full_path = os.path.join(source_path, path)
        if os.path.exists(full_path):
            return full_path

    return path


class _CompileCommand(gvrun.commands.Command):
    """
    Command for compiling a source file

    Once created, the command is enqueued to the builder in order to be scheduled, and called
    when one the worker wants to execute it.

    Attributes
    ----------
    builder : gvrun.commands.Builder
        Builder where commands should be enqueued.
    toolchain: Toolchain
        Toolchain used for compiling the source code
    flags: _ToolchainCFlags
        List of flags used for compiling the source code
    """
    def __init__(self, builder: gvrun.commands.Builder, toolchain: Toolchain,
            flags: _ToolchainCFlags):
        super().__init__(builder)

        self.flags = flags
        # Remember the current path to execute the compile command from there in case some
        # files are using relative path
        self.path = os.getcwd()

        if toolchain is None:
            raise RuntimeError('Trying to compile without any toolchain attached')

        self.command = toolchain._get_compile_command(flags)

    def run(self):
        """Execute the compilation command.

        Should be called by a builder worker then the command is ready to be executed.
        """
        print (f'CC  {self.flags.source_path}', flush=True)
        self.execute(self.command, self.path)


class _LinkCommand(gvrun.commands.Command):
    """
    Command for linking object files.

    Once created, the command is enqueued to the builder in order to be scheduled, and called
    when one the worker wants to execute it.

    Attributes
    ----------
    builder : gvrun.commands.Builder
        Builder where commands should be enqueued.
    toolchain: Toolchain
        Toolchain used for linking the object files
    flags: _ToolchainLdFlags
        List of flags used for linking the object files
    """
    def __init__(self, builder: gvrun.commands.Builder, toolchain: Toolchain,
            flags: _ToolchainLdFlags):
        super().__init__(builder)

        self.builder = builder
        self.flags = flags
        # Remember the current path to execute the compile command from there in case some
        # files are using relative path
        self.path = os.getcwd()

        if toolchain is None:
            raise RuntimeError('Trying to compile without any toolchain attached')

        self.command = toolchain._get_link_command(flags)

    def run(self):
        """Execute the link command.

        Should be called by a builder worker then the command is ready to be executed.
        """
        print (f'LD  {self.flags.binary}', flush=True)
        self.execute(self.command, self.path)


@dataclasses.dataclass
class _Define:
    """
    Container for defines

    Attributes
    ----------
    name (str): The name of the define.
    value (str | None): The value of the define. Can be None in case it has no value.
    internal (bool): True if the define should be used when compiling the sources of the container
        of the define.
    external (bool): True if the define should be used when compiling sources of a container using
        the container of the define.
    """
    name: str
    value: str | None = None
    internal: bool = False
    external: bool = False


@dataclasses.dataclass
class _Include:
    """
    Container for include folders

    Attributes
    ----------
    name (str): The path of the folder to be included.
    internal (bool): True if the include path should be used when compiling the sources of the
        container of the define.
    external (bool): True if the include path should be used when compiling sources of a container
        using the container of the define.
    """
    name: str
    internal: bool = False
    external: bool = False


class TemplateFile():
    """
    Container for template files

    A template file is a file which is used as a template to generate build-time files.
    Properties of the form @name@ can be attached to replace with runtime values.

    Attributes
    ----------
    name (str): The name of the template file
    parent (str): The source container owning this template
    path (str): The path of the generated file
    template (str): The path of the template file
    """
    def __init__(self, name: str, parent: "_SourceContainer", path: str, template: str):
        self.path = path
        self.parent = parent
        self.template = template
        self.parameters = {}
        parent._add_template_file(name, self)

    def add_parameter(self, name, value):
        """Add a parameter.

        Any string in the template file of the form @name@ will be replaced by the specified value.

        Parameters
        ----------
        name (str): Name of the property. Any string of the form @name@ in the template will be
            replaced by the value
        value (str): Value to be put in the generated file instead of the property.
        """
        self.parameters[name] = value

    def get_path(self):
        """Get the path of the generated file.

        Returns
        -------
        str
            The generated path.
        """
        return self.path

    def _gen(self):
        """Generate the output file

        Properties are replaced by their value in the template file and the output file is generated
        """
        os.makedirs(os.path.dirname(self.path), exist_ok=True)

        loaded_config = None
        if os.path.exists(self.path):
            with open(self.path, 'r') as file:
                loaded_config = file.read()

        with open(self.parent._get_source_path(self.template), 'r') as file:
            content = file.read()

            for prop_name, prop_value in self.parameters.items():
                content = content.replace(f'@{prop_name}@', str(prop_value))

        if content != loaded_config:
            with open(self.path, 'w') as file:
                file.write(content)

class _SourceContainer(gvrun.target.SystemTreeNode):
    """
    Container for sources

    Any kind of source container must inherit from this class. This can be executable, module,
    library and so on.

    Attributes
    ----------
    name (str): Name of the container. This name will contribute to the path of the container within
        the system tree.
    target (gvrun.target.SystemTreeNode | None): Target this container is attached to. The target can be
        used to query information about the target.
    parent (gvrun.target.SystemTreeNode | None): Parent of this container within the system tree. The
        parent can be either another source container or an architectural component.
    """

    def __init__(self, name: str, target: gvrun.target.SystemTreeNode | None=None,
            parent: gvrun.target.SystemTreeNode | None=None):
        super().__init__(name, parent=parent)
        self.__sources = []
        self.__source_paths = []
        self.__cflags = []
        self.__ldflags = []
        self.__lib_includes = []
        self.__defines = []
        self.__includes = []
        self.__template_files = {}
        # Stack of included config path, used to determine absolute path of the config files
        self.__path_stack = collections.deque()
        self.__toolchain = None

    def set_toolchain(self, toolchain: Toolchain):
        """Set the toolchain.

        The specified toolchain will be used to compile and link all the sources contained
        by this source container.

        Parameters
        ----------
        toolchain (Toolchain): Toolchain to be used.
        """
        self.__toolchain = toolchain

    def new_template_file(self, name: str, path: str, template: str) -> TemplateFile:
        """Declare a new template file.

        This will generate in the build folder a new file out of the specified template file
        where each attached property is used to replace any string of the form @name@ by the
        property value.

        Parameters
        ----------
        name (str): Name of the template file. This name is just used to reference the template
            file within its container
        path (str): Relative path of the generated file. This will be prefixed by the build folder
        template (str): Path of the template file
        """
        builddir = self.get_parameter('/builddir')
        if builddir is None:
            builddir = os.getcwd()
        propfile = TemplateFile(name, self, os.path.join(builddir, path),
            template)
        return propfile

    def add_define(self, name: str, value: str, internal: bool=True, external: bool=True):
        """Add a define.

        The define is attached to the container.
        If internal is True, it is defined when compiling sources inside the container.
        If external is True, it is defined when compiling sources depending on this container.

        Parameters
        ----------
        name (str): The name of the define.
        value (str | None): The value of the define. Can be None in case it has no value.
        internal (bool): True if the define should be used when compiling the sources of the
            container of the define.
        external (bool): True if the define should be used when compiling sources of a container
            using the container of the define.
        """
        self.__defines.append(_Define(name, value, internal, external))

    def add_includes(self, includes: list[str] | str, internal: bool=True, external:bool=True):
        """Add an include folder.

        This can be either a single include folder or a list of folders.
        The folders are attached to the container.
        If internal is True, they are included when compiling sources inside the container.
        If external is True, they are included when compiling sources depending on this container.

        Parameters
        ----------
        includes (list[str] | str): The set of include folders.
        internal (bool): True if the include paths should be used when compiling the sources of the
            container of the define.
        external (bool): True if the include paths should be used when compiling sources of a
            container using the container of the define.
        """
        if isinstance(includes, list):
            for include in includes:
                self.__includes.append(_Include(include, internal, external))
        else:
            self.__includes.append(_Include(includes, internal, external))

    def add_cflags(self, cflags: list[str] | str):
        """Add CFLAGS.

        The CFLAGS are for now applied to all sources included in the executable.
        CFLAGS can be a single string or a list of strings.

        Parameters
        ----------
        cflags (list[str] | str): The set of CFLAGS.
        """
        if isinstance(cflags, list):
            self.__cflags += cflags
        else:
            self.__cflags.append(cflags)

    def add_ldflags(self, ldflags: list[str] | str):
        """Add LDFLAGS.

        The LDFLAGS are for now applied to all sources included in the executable.
        LDFLAGS can be a single string or a list of strings.

        Parameters
        ----------
        ldflags (list[str] | str): The set of LDFLAGS.
        """
        if isinstance(ldflags, list):
            self.__ldflags += ldflags
        else:
            self.__ldflags.append(ldflags)

    def add_lib_includes(self, includes: list[str] | str):
        """Add library include paths.

        The library include paths are added to the link command to specify folders where to
        look for libraries.
        The includes can be a single folder or a list of folders.

        Parameters
        ----------
        includes (list[str] | str): The set of include folders.
        """
        if isinstance(includes, list):
            self.__lib_includes += includes
        else:
            self.__lib_includes.append(includes)

    def add_source_path(self, paths: list[str] | str):
        """Add a source path.

        Source paths are the folders where the builder will look for to get absolute paths
        of source codes.
        Paths can be a single folder or a list of folders

        Parameters
        ----------
        paths (list[str] | str): The set of source folders.
        """
        if isinstance(paths, list):
            self.__source_paths += paths
        else:
            self.__source_paths.append(paths)

    def add_sources(self, sources: list[str] | str):
        """Add sources to be compiled.

        Sources are attached to this container are are for now compiled withing the executable
        compilation.
        Sources can be a single file or a list of files

        Parameters
        ----------
        sources (list[str] | str): The set of sources.
        """
        if isinstance(sources, list):
            self.__sources += sources
        else:
            self.__sources.append(sources)

    def add_subdirectory(self, path: str, target: gvrun.target.SystemTreeNode):
        """Add a subdirectory.

        The config file from the specified subdirectory is imported and its declare method
        is called.
        This can be used to organize the whole compilation as a hierarchy of folders.

        Parameters
        ----------
        path (str): The path of the folder containing the config file to be imported.
        target (gvrun.target.SystemTreeNode): The target to be passed to the imported config.
        """
        logging.debug(f'{self._get_title(True)} Importing config (name: {path})')

        # If not already absolute, convert it to absolute
        if not os.path.isabs(path):
            # Either we already included a file and we take his path as base or it means
            # we are in the current folder
            if len(self.__path_stack) > 0:
                path = os.path.join(self.__path_stack[-1], path)
            else:
                path = os.path.join(os.getcwd(), path)

        file = os.path.join(path, 'config.py')

        try:
            spec = importlib.util.spec_from_file_location(file, file)
            if spec is None or spec.loader is None:
                raise RuntimeError(f"Unable to load spec for {file}")
            module = importlib.util.module_from_spec(spec)
            sys.modules["module.name"] = module
            spec.loader.exec_module(module)

        except FileNotFoundError as _exc:
            raise RuntimeError(f'Unable to open test configuration file: {file}') from _exc

        self.__path_stack.append(path)
        module.declare(target, self)
        self.__path_stack.pop()


    def _get_title_from_type(self, type_name: str, full_path=False) -> str:
        """ Returns title for logging for specified type

        Returns a string containing the kind of node, class name and path which can be used to see
        where we are in the hierarchy when dumping logs.
        """
        if full_path:
            path = self.get_path()
        else:
            path = self.get_name()
        title = f'{type_name}({self.__class__.__name__})'
        if path is not None:
            title = f'{path}: ' + title
        return title

    def _get_title(self, full_path=False) -> str:
        """ Returns title for logging

        Returns a string containing the kind of node, class name and path which can be used to see
        where we are in the hierarchy when dumping logs.
        """
        return self._get_title_from_type('Container', full_path)

    def _add_template_file(self, name: str, file: TemplateFile):
        """Attach a template file to this container

        When this container is generated, all the attached template files will be generated.

        Parameters
        ----------
        name (str): Name of the template file
        file (TemplateFile): Template file to be added
        """
        self.__template_files[name] = file

    def _get_toolchain(self) -> Toolchain | None:
        """Returns the toolchain associated to this container
        """
        # Return the toolchain of this source container if it has one otherwise try
        # to get it from the parent
        if self.__toolchain is not None:
            return self.__toolchain

        parent = self._get_parent()
        if parent is not None and isinstance(parent, _SourceContainer):
            return parent._get_toolchain()

        return None

    def _get_lib_includes(self) -> list[str]:
        """Return the list of library include folders
        """
        return self.__lib_includes

    def _get_ldflags(self) -> list[str]:
        """Return the list of LDFLAGS

        This go through all childs to find the whole set.
        """
        ldflags = []
        for child in self._get_childs():
            ldflags += child._get_ldflags()
        ldflags += self.__ldflags
        return ldflags

    def _get_cflags(self) -> list[str]:
        """Return the list of CFLAGS

        This go through all childs to find the whole set.
        """
        cflags = []
        for child in self._get_childs():
            cflags += child._get_cflags()
        cflags += self.__cflags
        return cflags

    def _get_source_paths(self) -> list[str]:
        """Return the list of source include paths

        This go through all childs to find the whole set.
        """
        paths = []

        parent = self._get_parent()
        if parent is not None and isinstance(parent, _SourceContainer):
            paths += parent._get_source_paths()

        paths += self.__source_paths

        return paths

    def _get_source_path(self, source: str) -> str:
        """Return the absolute path of a source file

        The specified file is searche into the source paths and the absolute path within the
        source path is returned.
        If the file is nto found in the source paths, its path is returned as it is.
        """
        source_paths = self._get_source_paths()

        for source_path in source_paths:
            path = os.path.join(source_path, source)
            if os.path.exists(path):
                return path

        return source

    def _get_sources(self) -> list[str]:
        """Returns the sources to be compiled

        This go through all childs to find the whole set.
        """
        sources = []
        for source in self.__sources:
            sources.append([source, self._get_source_path(source)])

        for child in self._get_childs():
            sources += child._get_sources()

        return sources

    def _get_defines(self, internal: bool=False, external: bool=False) -> list[str]:
        """Returns the defines to be used for compiling

        This go through all childs to find the whole set.
        """
        defines = []
        for child in self._get_childs():
            defines += child._get_defines(internal=False, external=True)

        for define in self.__defines:
            if define.internal and internal or define.external and external:
                defines.append(define)

        return defines

    def _get_includes(self, internal: bool=False, external: bool=False) -> list[str]:
        """Returns the includes to be used for compiling

        This go through all childs to find the whole set.
        """
        includes = []
        for child in self._get_childs():
            includes += child._get_includes(internal=False, external=True)

        for include in self.__includes:
            if include.internal and internal or include.external and external:
                includes.append(include)

        return includes

    def _get_compile_commands(self, builder: gvrun.commands.Builder, builddir: str) \
            -> list[_CompileCommand]:
        """Returns the compile commands

        This go through all childs to find the whole set.
        """
        toolchain = self._get_toolchain()

        if toolchain is None:
            raise RuntimeError(
                f'{self._get_title(True)} Trying to compile without any toolchain attached')

        commands = []
        for source in self.__sources:
            deps = []
            source_path = self._get_source_path(source)
            path = os.path.join(builddir, source.rstrip('.c').rstrip('.S') + '.o')

            dep_file = os.path.join(builddir, source.rstrip('.c').rstrip('.S') + '.d')
            if os.path.exists(dep_file):
                with open(dep_file, 'r') as file:
                    content = file.read().replace("\\\n", " ")

                    for line in content.splitlines():
                        if ":" not in line:
                            continue

                        target, deps_str = line.split(":", 1)

                        deps += deps_str.split()
            else:
                deps.append(source_path)

            obj_timestamp = -1
            if os.path.exists(path):
                obj_timestamp = os.path.getmtime(path)

            for dep in deps:
                src_timestamp = os.path.getmtime(dep)

                if src_timestamp >= obj_timestamp:

                    flags = _ToolchainCFlags(
                        builddir=builddir,
                        source_name=source,
                        source_path=source_path,
                        cflags=self._get_cflags(),
                        includes=self._get_includes(internal=True),
                        defines=self._get_defines(internal=True)
                    )

                    commands.append(
                        _CompileCommand(builder, toolchain, flags)
                    )

        return commands

    def _compile(self, builder: gvrun.commands.Builder, builddir: str):
        """Compile step for this container

        Generates associated template files
        """
        for propfile in self.__template_files.values():
            propfile._gen()


class PulposModule(_SourceContainer):
    """
    Container for Pulpos module

    Any target should define a class which inherits from this one to customize it
    with target-dependent features and provide a fully featured Pulpos module to the application.

    Attributes
    ----------
    target (gvrun.target.Target): Target this container is attached to. The target can be
        used to query information about the target.
    parent (_SourceContainer): Parent of this container within the system tree. The
        parent must be another source container.
    """

    def __init__(self, target: gvrun.target.SystemTreeNode, parent: _SourceContainer):
        super().__init__('pulpos', target, parent=parent)


    def _dump_tree(self, tree: rich.tree.Tree, inc_arch: bool, inc_build: bool, inc_target: bool,
            inc_attr: bool, inc_prop: bool):
        """Dump the tree of parameters, attributes and properties
        """
        if self._has_tree_content():
            new_tree = tree.add(
                self._dump_parameter_title(self.get_name(), 'Module', self.__class__.__name__))
            self._dump_node_parameters(new_tree, inc_arch, inc_build, inc_target, inc_attr,
                inc_prop)
            for child in self._get_childs():
                child._dump_tree(new_tree, inc_arch, inc_build, inc_target, inc_attr, inc_prop)

    def _get_title(self, full_path: bool=False):
        """Returns a title for this node suitable for logging
        """
        return self._get_title_from_type('Module', full_path)


class PulposExecutable(_SourceContainer, gvrun.target.Executable):
    """
    Container for Pulpos-based executable

    Any target should define a class which inherits from this one to customize it
    with target-dependent features and provide a fully featured Pulpos executable to the
    application.

    Attributes
    ----------
    name (str): Name of the executable
    target (gvrun.target.SystemTreeNode): Target this container is attached to. The target can be
        used to query information about the target.
    parent (_SourceContainer): Parent of this container within the system tree. The
        parent must be another source container.
    """

    def __init__(self, name: str, target: gvrun.target.SystemTreeNode):
        super().__init__(name, target=target, parent=target)
        self.__target = target
        builddir = self.get_parameter('/builddir')
        if builddir is None:
            builddir = os.getcwd()
        self.__builddir = os.path.join(builddir, self.get_path())
        self.__binary = os.path.join(self.__builddir, name)

        target._add_executable(self)


    def get_binary(self) -> str:
        """Get the executable binary.

        The compilation of the executable will produce a binary whose path can be retrieved by
        calling this function.

        Returns
        -------
        str: The binary path
        """
        return self.__binary

    def _dump_tree(self, tree: rich.tree.Tree, inc_arch: bool, inc_build: bool, inc_target: bool,
            inc_attr: bool, inc_prop: bool):
        """Dump the tree of parameters, attributes and properties
        """
        if self._has_tree_content():
            new_tree = tree.add(
                self._dump_parameter_title(self.get_name(), 'Executable', self.__class__.__name__))
            self._dump_node_parameters(new_tree, inc_arch, inc_build, inc_target, inc_attr,
                inc_prop)
            for child in self._get_childs():
                child._dump_tree(new_tree, inc_arch, inc_build, inc_target, inc_attr, inc_prop)

    def _get_title(self, full_path: bool=False) -> str:
        """Returns a title for this node suitable for logging
        """
        return self._get_title_from_type('Executable', full_path)

    def _compile(self, builder: gvrun.commands.Builder, builddir: str):
        """Compile the executable
        """
        toolchain = self._get_toolchain()

        if toolchain is None:
            raise RuntimeError(
                f'{self._get_title(True)} Trying to link without any toolchain attached')

        commands = self._get_compile_commands(builder, self.__builddir)

        for child in self._get_childs():
            commands += child._get_compile_commands(builder, self.__builddir)

        do_link = len(commands) != 0 or not os.path.exists(self.__binary)

        if do_link:
            flags = _ToolchainLdFlags(
                builddir=self.__builddir,
                binary=self.__binary,
                sources=self._get_sources(),
                ldflags=self._get_ldflags(),
                includes=self._get_lib_includes()
            )

            link_command = _LinkCommand(builder=builder, toolchain=toolchain, flags=flags)

            if len(commands) != 0:
                for command in commands:
                    command.add_trigger(link_command)
                    builder.push_command(command)
            else:
                builder.push_command(link_command)


def new_executable(name: str, target: gvrun.target.SystemTreeNode):
    """Create a new executable

    This will create a new executable for the specified target.

    Parameters
    ----------
    name (str): Name of the executable.
    target (gvrun.target.SystemTreeNode): Target for which this executable will be compiled.
    """
    target_name = target.get_target_name()
    if target_name is None:
        raise RuntimeError('Could not load Pulpos target config, the target has no target name')

    try:
        module = importlib.import_module('pulpos.' + target_name)
    except ModuleNotFoundError as exc:
        raise RuntimeError(f'The target has no pulpos descriptor (target_name: {target_name}, missing module: pulpos.{target_name}.py)') from exc

    return module.new_executable(name, target)
