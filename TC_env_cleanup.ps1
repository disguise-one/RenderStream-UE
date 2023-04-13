param(
    [string[]] [Parameter (Position = 2, ValueFromRemainingArguments )] $RemainingArgs
)

# Script that cleans up the symbolic link created in the TC env setup.
# Can be exapnded to do more things in the future.
function remove-sybolic_link {

    # delete drive and symbolic link
    if (($env:computername -like "builds-agent-*") -or ($debug_force_remote_disk -eq $true)){
        (get-item ..\..\link_to_unreal\).Delete()
        Get-PSDrive K -ErrorAction SilentlyContinue | Remove-PSDrive -ErrorAction SilentlyContinue
    }
}

remove-sybolic_link
