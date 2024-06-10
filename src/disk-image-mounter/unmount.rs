use async_recursion::async_recursion;
use std::{collections::HashMap, vec};

use udisks::zbus;
//TODO: these functions belong to gduutils, they should be moved there when it is rewritten in
//Rust

#[async_recursion]
pub async fn unuse_data_iterate(
    client: &udisks::Client,
    object: &udisks::Object,
) -> zbus::Result<()> {
    let (filesystem_to_unmount, encrypted_to_lock, _last) =
        is_in_full_use(client, object, false).await?;
    //let block = object.block().await;
    //
    //if block.is_ok() && (filesystem_to_unmount.is_some() || encrypted_to_lock.is_some()) {
    //    let block = block.unwrap();
    //    if let Ok(loop_) = client.loop_for_block(&block).await {
    //        if loop_.autoclear().await.is_ok_and(|res| res) {
    //            let loop_object = client.object(loop_.inner().path().clone())?;
    //            let (_fs, _block, last) = is_in_full_use(client, &loop_object, true).await?;
    //            dbg!(last);
    //            if last {
    //                loop_.set_autoclear(false, HashMap::new()).await?;
    //                unuse_data_iterate(client, object).await?;
    //                return Ok(());
    //            }
    //        }
    //    }
    //}

    if let Some(filesystem_to_unmount) = filesystem_to_unmount {
        filesystem_to_unmount.unmount(HashMap::new()).await?;
    } else if let Some(encrypted_to_lock) = encrypted_to_lock {
        encrypted_to_lock.lock(HashMap::new()).await?;
        unuse_data_iterate(client, object).await?;
    }
    Ok(())
}

pub async fn is_in_full_use(
    client: &udisks::Client,
    object: &udisks::Object,
    last_out: bool,
) -> zbus::Result<(
    Option<udisks::filesystem::FilesystemProxy<'static>>,
    Option<udisks::encrypted::EncryptedProxy<'static>>,
    bool,
)> {
    let objects_to_check = all_contained_objects(client, object).await;

    let mut filesystem_to_unmount = None;
    let mut encrypted_to_lock = None;
    let mut ret = false;
    let mut last = true;

    // check in reverse order, e.g. cleartext before LUKS, partitions before the main block device
    for object_iter in objects_to_check.iter().rev() {
        let Ok(block_for_object) = object_iter.block().await else {
            continue;
        };
        if let Ok(filesystem_for_object) = object_iter.filesystem().await {
            let mount_points = filesystem_for_object.mount_points().await?;
            if mount_points.iter().flatten().count() > 0 {
                if ret {
                    last = false;
                    break;
                }
                filesystem_to_unmount = Some(filesystem_for_object);
                ret = true;
            }
        }

        if let Ok(encrypted_for_object) = object_iter.encrypted().await {
            if client.cleartext_block(&block_for_object).await.is_some() {
                if ret {
                    last = false;
                    break;
                }
                encrypted_to_lock = Some(encrypted_for_object);
                ret = true;
            }
        }

        if ret && !last_out {
            break;
        }
    }

    Ok((filesystem_to_unmount, encrypted_to_lock, last))
}

async fn all_contained_objects(
    client: &udisks::Client,
    object: &udisks::Object,
) -> Vec<udisks::Object> {
    let mut objects_to_check = vec![];

    let block = if let Ok(drive) = object.drive().await {
        client.block_for_drive(&drive, false).await
    } else {
        object.block().await.ok()
    };

    if let Some(block) = block {
        if let Ok(block_object) = client.object(block.inner().path().clone()) {
            objects_to_check.push(block_object.clone());
            // if we're a partitioned block device, add all partitions
            if let Ok(partition_table) = block_object.partition_table().await {
                objects_to_check.extend(
                    client
                        .partitions(&partition_table)
                        .await
                        .iter()
                        .filter_map(|partition| {
                            client.object(partition.inner().path().clone()).ok()
                        }),
                );
            }
        }
    }

    // add LUKS objects
    let mut i = 0;
    while i < objects_to_check.len() {
        let object_iter = &objects_to_check[i];
        if let Ok(block_for_object) = object_iter.block().await {
            if let Some(cleartext) = client.cleartext_block(&block_for_object).await {
                if let Ok(cleartext_object) = client.object(cleartext.inner().path().clone()) {
                    objects_to_check.push(cleartext_object);
                }
            }
        }
        i += 1;
    }
    objects_to_check
}
