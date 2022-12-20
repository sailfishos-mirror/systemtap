# Language Server
*This document is still a WIP*

## Getting Started
Welcome to the Systemtap language server guide. The language server implements the [language-server-protocol](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/) and currently supports the following features
* **Lifecycle messages**
  * initialize
  * initialized
  * shutdown
  * exit 
* **Document Synchronization**
  * textDocument/DidOpen
  * textDocument/DidChange
  * textDocument/DidSave
  * textDocument/DidClose
* **Language Featuress**
  * textDocument/completion

*When features are added, make sure to update the above*

## Usage
The language-server, which can be run with `stap --language-server`, will start a process that will communicate 
with the **parent** language-server-client using stdio. Below describes some tested clients, and how to set them up to run the stap language server

### Vim
- Install the [Minimalist Vim plugin manager](https://github.com/junegunn/vim-plug)
```bash
curl -fLo ~/.vim/autoload/plug.vim --create-dirs \
    https://raw.githubusercontent.com/junegunn/vim-plug/master/plug.vim
```
- Add the following to .vimrc
```vim
" The Vim Language-Server plugin
call plug#begin()
Plug 'prabirshrestha/vim-lsp'
Plug 'prabirshrestha/asyncomplete.vim'
Plug 'prabirshrestha/asyncomplete-lsp.vim'
call plug#end()

" Optional logs
" let g:lsp_log_verbose = 1
" let g:lsp_log_file = expand('vim-lsp.log')

" Tab Completion
" Tab and Shift-Tab back and forth through completion options
" Tab Ctrl-y to accept a completion
inoremap <expr> <Tab> pumvisible() ? "\<C-n>" : "\<Tab>"
inoremap <expr> <S-Tab> pumvisible() ? "\<C-p>" : "\<S-Tab>"
inoremap <expr> <cr> pumvisible() ? "\<C-y>\<cr>" : "\<cr>"

" Force refresh completion
imap <c-space> <Plug>(asyncomplete_force_refresh)

autocmd! CompleteDone * if pumvisible() == 0 | pclose | endif

if (executable('stap'))
    autocmd User lsp_setup call lsp#register_server({
        \ 'name': 'systemtap-language-server',
        \ 'cmd': {server_info->['stap', '--language-server']},
        \ 'whitelist': ['stp']
        \ })
endif
```
- In vim command mode execute `PlugInstall`
- For more details
    - https://github.com/prabirshrestha/asyncomplete.vim
    - https://github.com/prabirshrestha/vim-lsp/blob/master/doc/vim-lsp.txt

### Jupyter
* Install the [Jupyter LSP extension](https://github.com/jupyter-lsp/jupyterlab-lsp)
* Make a config file
```json
{
  "LanguageServerManager": {
    "language_servers": {
      "stap-language": {
        "version": 2,
        "argv": ["stap", "--language-server"],
        "debug_argv": ["stap", "--language-server", "-v"],
        "languages": ["systemtap"],
        "mime_types": ["text/systemtap", "text/x-systemtap"],
        "display_name": "systemtap-language-server"
      }
    }
  }
}
```
* Copy the config file to the jupyter config path, using something like the following
then enable the extension
```bash
#!/bin/bash

CONFIG_FILE="jupyter_stap_lsp.json"
JCONFIG=`jupyter --config`
cp $CONFIG_FILE "$JCONFIG/jupyter_server_config.d/$CONFIG_FILE"

jupyter server extension enable --user --py jupyter_lsp
```

### VSCode
*This is the development usage, and a proper vscode extension should be published*

#### Development Directory Structure
```
vscode-workspace-root-directory
├── .vscode
│   ├── launch.json    // Launch configuration
│   ├── settings.json  
│   └── tasks.json     // Workspace-specific configurations
│
├── vscode_client // Language Client
│   └── src
│       └── vscode_stap_lsp.ts // Language Client entry point
│
├── package.json // The extension manifest
│
└── tsconfig.json //TypeScript compiler options
```

#### Requirements
* VSCode
* vscode-python extension
    * Install with `Ctrl-P` then `ext install ms-python.python`

#### Usage
*The above files should be in the root of the vscode current working directory*
1. Run `npm install package.json`  
2. Open debug view (`ctrl + shift + D`)
3. Select `Launch Client` and press `Start Debbuging` (Play Button / F5)

*If you use a another client, please feel free to submit a patch to the above with the added usage recipe*