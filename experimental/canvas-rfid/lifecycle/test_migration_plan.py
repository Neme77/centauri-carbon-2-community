"""Manifest rewrite boundary tests; no printer or executable input."""
from installed_profiles import UnsupportedProfile
from migration_plan import update_manifest

def run():
    old='1'*64;new='2'*64;other='3'*64
    count=0
    for prefix in ('','./'):
        for separator in ('  ',' *'):
            data=(old+separator+prefix+'launcher.sh\n'+other+'  unchanged.sh\n').encode()
            assert update_manifest(data,{'launcher.sh':(old,new)})==data.replace(old.encode(),new.encode(),1)
            count+=1
    for data,changes in [
        ((old+'  launcher.sh\n'+old+'  ./launcher.sh\n').encode(),{'launcher.sh':(old,new)}),
        ((old+'  launcher.sh\n').encode(),{'absent':(old,new)}),
        ((old+'  launcher.sh\n').encode(),{'launcher.sh':(other,new)}),
        ((old+'  launcher.sh').encode(),{'launcher.sh':(old,new)}),
        (b'invalid\n',{'launcher.sh':(old,new)}),
    ]:
        try:update_manifest(data,changes)
        except UnsupportedProfile:count+=1
        else:raise AssertionError('Unsafe manifest accepted')
    print(f'PASS: {count} exact manifest rewrite/refusal cases')
    return count

if __name__=='__main__':run()
