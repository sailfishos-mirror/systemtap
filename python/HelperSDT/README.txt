Developer Note
==============

The default prefix for developer builds is '/usr/local', the default
prefix for production builds is '/usr'. This module ends up getting
installed in
'$(prefix)/lib/python${PYTHON_VER}/site-packages/HelperSDT'. For a
developer build for python 3.14, that would end up being the following
directory: '/usr/local/lib/python3.14/site-packages/HelperSDT'. The
problem is the '/usr/local/lib/python3.14/site-packages' directory
isn't always in the default python path on RHEL/Fedora.

Observe the following from a typical system (as root):

====
# python3 -m site
sys.path = [
    '/root',
    '/usr/lib64/python314.zip',
    '/usr/lib64/python3.14',
    '/usr/lib64/python3.14/lib-dynload',
    '/usr/lib64/python3.14/site-packages',
    '/usr/lib/python3.14/site-packages',
]
USER_BASE: '/root/.local' (doesn't exist)
USER_SITE: '/root/.local/lib/python3.14/site-packages' (doesn't exist)
ENABLE_USER_SITE: True
====

So, when doing a developer build, we've got to arrange for the
installation directory to appear in the python path. There are (at
least) 2 ways of doing this:

1) Put the developer build installation directory in the PYTHONPATH
environment variable.

====
# PYTHONPATH=/usr/local/lib/python3.14/site-packages python3 -m site
sys.path = [
    '/root',
    '/usr/local/lib/python3.14/site-packages',
    '/usr/lib64/python314.zip',
    '/usr/lib64/python3.14',
    '/usr/lib64/python3.14/lib-dynload',
    '/usr/lib64/python3.14/site-packages',
    '/usr/lib/python3.14/site-packages',
]
USER_BASE: '/root/.local' (doesn't exist)
USER_SITE: '/root/.local/lib/python3.14/site-packages' (doesn't exist)
ENABLE_USER_SITE: True
====

To make that change permanent, add
'export PYTHONPATH=/usr/local/lib/python3.14/site-packages' to the user's
~/.profile. Then log out and back in again.

Note that this has to be done for every user that wants to use the
HelperSDT module.


2) Add a file, 'local.pth', to your 'USER_SITE'
directory (by default '~/.local/lib/python3.14/site-packages').
Notice the USER_SITE directory specified above in the
'python3 -m site' output. That file is just a list of directories to
add to the python path.

====
# python3 -m site
sys.path = [
    '/home/dsmith',
    '/usr/lib64/python314.zip',
    '/usr/lib64/python3.14',
    '/usr/lib64/python3.14/lib-dynload',
    '/usr/lib64/python3.14/site-packages',
]
USER_BASE: '/home/dsmith/.local' (doesn't exist)
USER_SITE: '/home/dsmith/.local/lib/python3.14/site-packages' (doesn't exist)
ENABLE_USER_SITE: True
# mkdir -p ~/.local/lib/python3.14/site-packages
# echo '/usr/local/lib/python3.14/site-packages' > ~/.local/lib/python3.14/site-packages/local.pth
# python3 -m site
sys.path = [
    '/home/dsmith',
    '/usr/lib64/python314.zip',
    '/usr/lib64/python3.14',
    '/usr/lib64/python3.14/lib-dynload',
    '/home/dsmith/.local/lib/python3.14/site-packages',
    '/usr/local/lib/python3.14/site-packages',
    '/usr/lib64/python3.14/site-packages',
    '/usr/lib/python3.14/site-packages',
]
USER_BASE: '/home/dsmith/.local' (exists)
USER_SITE: '/home/dsmith/.local/lib/python3.14/site-packages' (exists)
ENABLE_USER_SITE: True
====

Note that this has to be done for every user that wants to use the
HelperSDT module. (This could be done system-wide by putting the .pth
file in a system python path like /usr/lib/python3.14/site-packages.)

Method 1) (setting the environment variable) is a bit more visible
(since you can easily see the environment variable), but a bit more
inconvenient since you have to log in/out for changes to take
effect. Method 2) (adding the .pth file) is fairly easy, but a bit
more 'magical' since it is easy to forget that file in a hidden
directory exists.

Also note that changing the python path must be undone when
using/testing a production build installation, since you don't want 2
copies of the same module in the python path (which is another vote
for Method 2 since it is easier to undo).
