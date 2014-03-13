from distutils.core import setup, Extension
 
module1 = Extension('pk2serial', sources = ['main.c'], extra_link_args = ['-lusb'])

setup (name = 'PyPk2Serial',
        version = '1.0',
        description = 'Enables PICKIT2 UART module in Python',
        ext_modules = [module1])
