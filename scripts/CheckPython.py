import subprocess
from importlib import metadata

def InstallPackage(package):
    print(f"Installing {package} module...")
    subprocess.check_call(['python', '-m', 'pip', 'install', package])

# importlib.metadata, not pkg_resources: setuptools 81+ no longer ships pkg_resources.
def ValidatePackage(package):
    try:
        metadata.distribution(package)
    except metadata.PackageNotFoundError:
        InstallPackage(package)

def ValidatePackages():
    ValidatePackage('requests')
    ValidatePackage('fake-useragent')
    ValidatePackage('colorama')
