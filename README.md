# LydiaSyft: A Compositional Symbolic Synthesis Framework for LTLf Specification

LydiaSyft is an open-source software framework for reasoning and synthesis of Linear Tempora Logic formulas interpreted on finite traces (LTLf), integrating efficient data structures and techniques focused on LTLf specifications. 

This project can be used either as a standalone CLI tool or as a C++ library that integrates with other projects. Among the implemented Data Structures and algorithms, we have:

- DFA representation and manipulation:
  - Explicit-state DFA (à la MONA): [(N. Klarlund et al., 2002)](https://www.worldscientific.com/doi/abs/10.1142/S012905410200128X), [(De Giacomo and Favorito, 2021)](https://ojs.aaai.org/index.php/ICAPS/article/view/15954)
  - Symbolic-state DFA: [(Zhu et al., 2017)](https://www.ijcai.org/proceedings/2017/0189)

- LTLf synthesis settings:
  - Classical synthesis: [(Zhu et al., 2017)](https://www.ijcai.org/proceedings/2017/0189)
  - MaxSet synthesis: [(Zhu and De Giacomo, 2022)](https://www.ijcai.org/proceedings/2022/386)
  - Synthesis with fairness assumptions: [(Zhu et al., 2020)](https://ojs.aaai.org/index.php/AAAI/article/view/5704)
  - Synthesis with stability assumptions: [(Zhu et al., 2020)](https://ojs.aaai.org/index.php/AAAI/article/view/5704)
  - Synthesis with environment GR(1) assumptions: [(De Giacomo et al., 2022)](https://link.springer.com/article/10.1007/s10703-023-00413-2)

  
Currently, the system has been tested on Ubuntu 24.04 LTS, and should work on other Linux systems. We plan to fully support also MacOS and Windows systems.


## Dependencies

The software depends on the following projects:

- CUDD: CU Decision Diagram package: https://github.com/KavrakiLab/cudd
- MONA (WhiteMech's fork): https://github.com/whitemech/MONA
- Lydia: https://github.com/whitemech/lydia
- Flex & [Bison](https://www.gnu.org/software/bison/)
- [Graphviz](https://graphviz.org/)
- Syfco: the Synthesis Format Conversion Tool: https://github.com/reactive-systems/syfco
- Slugs: SmalL bUt Complete GROne Synthesizer: https://github.com/VerifiableRobotics/slugs/


## Compilation Instructions using CMake

### System-wide dependencies

The instructions have been tested over a machine with Ubuntu 24.04 as operating system.

First, install the following system-wide dependencies:

```
sudo apt install -y \
   automake         \
   build-essential  \
   cmake            \
   curl             \
   libtool          \
   wget             \
   unzip
```


### Install Flex, Bison

Install flex and bison:

    sudo apt-get install flex bison

### Install Graphviz

For the graphical features (automata and strategy visualization), graphviz need to be installed:

```
sudo apt install graphviz libgraphviz-dev
```

### Install Syfco

Building Syfco requires the Glasgow Haskell Compiler. To install the tool you can use `stack`:

```
curl -sSL https://get.haskellstack.org/ | sh
git clone https://github.com/reactive-systems/syfco.git
cd syfco
git checkout 50585e0
stack install
```

The installation should install the `syfco` binary in a directory in teh system path. 
Then, make sure the binary `syfco` can be found on your system path: `which syfco`.

When using the CLI, you can also provide the path to the `syfco` binary manually by setting `--syfco-path`. 

### Install Slugs (Optional)

Slugs is required for solving LTLf synthesis with GR(1) conditions.

To build Slugs, run:

```
cd submodules/slugs
git checkout a188d83
cd src
make -j$(nproc --ignore 1)
```

### Install CUDD

Make sure CUDD is installed. CUDD can be found at:

    https://github.com/KavrakiLab/cudd.git

Install CUDD:

    autoreconf -f -i
    ./configure --enable-silent-rules --enable-obj --enable-dddmp --prefix=/usr/local
    sudo make install

### Install Mona

To install MONA system-wide:

```
git clone --recursive https://github.com/whitemech/MONA.git
cd MONA
git checkout v1.4-19.dev0
./configure && make -j && sudo make -j install
# copy headers manually
sudo mkdir -p /usr/local/include/mona
sudo cp Mem/mem.h Mem/gnuc.h Mem/dlmalloc.h BDD/bdd_external.h BDD/bdd_dump.h BDD/bdd_internal.h BDD/bdd.h BDD/hash.h DFA/dfa.h GTA/gta.h config.h /usr/local/include/mona
```


### Install Lydia

The tool requires the installation of Lydia, which will be triggered by the CMake configuration.

However, if you want to install Lydia manually, you can co into `submodules/lydia` and follow the installation
instructions in the `README.md`.

### Install Z3

By default, the CMake configuration will fetch z3 automatically from the GitHub repository.
There might be required other dependencies. 

In order to disable this behaviour, you can configure the project by setting `-DZ3_FETCH=OFF`.
In that case, you have to have the library installed on your system.
To link the static library of z3, you have to install z3 manually:

```
wget https://github.com/Z3Prover/z3/releases/download/z3-4.8.12/z3-4.8.12-x64-glibc-2.31.zip
unzip z3-4.8.12-x64-glibc-2.31.zip
cd z3-4.8.12-x64-glibc-2.31
sudo cp bin/libz3.a /usr/local/lib
sudo cp include/*.h /usr/local/include
```

## Build LydiaSyftEL

1. Add `submodules` using: i. `git clone https://github.com/GianmarcoDIAG/lydia.git --recursive` ii. `git clone https://github.com/VerifiableRobotics/slugs --recursive` and iii. `https://github.com/jothepro/doxygen-awesome-css.git --recursive` (note that standard version of Lydia does not work. You need Lydia's version in the project mention in i.)
2. `mkdir build && cd build`
3. `cmake .. && make -j2`

## Run LydiaSyftEL

This is the output of `LydiaSyftEL --help`

```
LydiaSyft-EL: A compositional synthesizer of LTLf+
Usage: ./LydiaSyftEL [OPTIONS]

Options:
  -h,--help                   Print this help message and exit
  -i,--input-file TEXT:FILE REQUIRED
                              Path to LTLf+ formula file
  -p,--partition-file TEXT:FILE REQUIRED
                              Path to partition file
  -s,--starting-player INT REQUIRED
                              Starting player:
                              agent=1;
                              environment=0.
```

Directory `examples/ltlfplus` provide two running examples. Use the following:

1. `./LydiaSyftEL -i test.ltlfplus -p test.part -s 1` # Realizable
2. `./LydiaSyftEL -i test2.ltlfplus -p test2.part -s 1` # Unrealizable
