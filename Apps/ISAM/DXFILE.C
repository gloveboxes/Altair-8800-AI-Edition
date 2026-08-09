#include "stdio.h"
#include "ISAMDB.H"

int i_mktbl(char *table_name)
{
    int file_descriptor;
    int rc;
    int table_index;
    char filename[20];

    table_index = find_table_index(table_name);
    if (table_index < 0)
        return table_index;
    invalidate_table_files();
    remove_legacy_table_file(table_index, "DAT");
    remove_legacy_table_file(table_index, "IDX");
    remove_legacy_table_file(table_index, "JRN");
    remove_legacy_table_file(table_index, "MAP");
    build_table_filename(table_index, filename, "DAT");
    file_descriptor = open(filename, O_CREAT | O_TRUNC | O_WRONLY, 0);
    if (file_descriptor == ERROR)
        return I_EOPEN;
    close(file_descriptor);
    rc = pki_create(table_name);
    if (rc != I_OK)
        return rc;
    return clear_journal(table_index);
}

#define close(file_descriptor) close_table_file(file_descriptor)

/* Insert record - append to table data file */
int i_insrt(char *tblnam, char *rec, int rsiz)
{
    int fd;
    int i, tsz, nbytes;
    int rc;
    int phys;
    int reuse;
    int reuse_phys;
    int next_free_slot;
    long offset;
    char *sbuf;
    struct persistent_metadata metadata;
    struct persistent_metadata new_metadata;
    
    sbuf = record_workspace;
    i = find_table_index(tblnam);
    if (i < 0)
        return i;
    rc = load_table_metadata(i, &metadata);
    if (rc != I_OK)
        return rc;
            
            /* Verify record size matches table definition */
            tsz = g_cfg.tbls[i].recsz;
            if (rsiz != tsz)
                return I_ESIZE;

            rc = key_find(tblnam,
                &rec[g_cfg.tbls[i].keyoff[0]], 0);
            if (rc >= 0)
                return I_EDUP;
            if (rc != I_ENREC)
                return rc;
            
            /* Open for read/write */
            fd = open_table_file(i, "DAT", O_RDWR);
            if (fd == ERROR)
                return I_EOPEN;
            
            nbytes = rsiz;
            if (nbytes > I_RECSZ)
            {
                close(fd);
                return I_EWRIT;
            }
            reuse = metadata.free_head >= 0;
            reuse_phys = metadata.free_head;
            next_free_slot = -1;
            if (reuse && reuse_phys >= 0)
            {
                phys = reuse_phys;
                offset = (long)phys * nbytes;
                if (lseek(fd, offset, 0) < 0L)
                {
                    close(fd);
                    return I_EWRIT;
                }
                if (read(fd, sbuf, nbytes) != nbytes ||
                    sbuf[0] != I_DELFLAG || lseek(fd, offset, 0) < 0L)
                {
                    close(fd);
                    return I_EINDEX;
                }
                next_free_slot = read_free_link(sbuf);
                if (next_free_slot == reuse_phys ||
                    next_free_slot >= metadata.high_water_slot)
                {
                    close(fd);
                    return I_EINDEX;
                }
            }
            else
            {
                phys = metadata.high_water_slot;
                offset = (long)phys * nbytes;
                if (lseek(fd, offset, 0) < 0L)
                {
                    close(fd);
                    return I_EWRIT;
                }
                memset(sbuf, 0, nbytes);
            }

            new_metadata = metadata;
            new_metadata.index_count++;
            new_metadata.active_count++;
            new_metadata.generation++;
            if (reuse)
                new_metadata.free_head = next_free_slot;
            else
                new_metadata.high_water_slot = phys + 1;
            rc = write_journal(i, JOURNAL_OPERATION_INSERT, phys, reuse,
                sbuf, &metadata);
            if (rc != I_OK)
            {
                close(fd);
                return rc;
            }
            if (simulate_crash_after(I_CRASH_AFTER_JOURNAL))
            {
                close(fd);
                return I_EWRIT;
            }
            
            if (write(fd, rec, nbytes) != nbytes)
            {
                close(fd);
                return abort_journal(i, I_EWRIT);
            }
            if (fsync(fd) != 0)
            {
                close(fd);
                return abort_journal(i, I_EWRIT);
            }
            if (simulate_crash_after(I_CRASH_AFTER_DATA))
            {
                close(fd);
                return I_EWRIT;
            }

            defer_table_file_invalidation = 1;
            rc = key_add(tblnam, rec, phys);
            defer_table_file_invalidation = 0;
            if (rc != I_OK)
            {
                close(fd);
                return abort_journal(i, rc);
            }
            new_metadata.index_count = g_cfg.tbls[i].idxcnt;
            if (simulate_crash_after(I_CRASH_AFTER_DERIVED))
            {
                invalidate_table_files();
                close(fd);
                return I_EWRIT;
            }
            rc = save_table_metadata(i, &new_metadata);
            if (rc != I_OK)
            {
                close(fd);
                return abort_journal(i, rc);
            }
            rc = commit_journal(i);
            if (rc != I_OK)
            {
                close(fd);
                return abort_journal(i, rc);
            }
            if (simulate_crash_after(I_CRASH_AFTER_METADATA))
            {
                close(fd);
                return I_EWRIT;
            }
            rc = clear_journal(i);
            if (rc != I_OK)
            {
                close(fd);
                return rc;
            }
            
            close(fd);
            return I_OK;
}

/* Find physical index of Nth logical (non-deleted) record */
int i_findlog(char *tblnam, int logidx, int *physidx)
{
    int fd;
    int i, tsz, nbytes;
    int phys, logical;
    long offset;
    char *sbuf;

    stats_add(&i_runtime_stats.table_scans, 1);
    sbuf = record_workspace;
    i = find_table_index(tblnam);
    if (i < 0)
        return i;
            tsz = g_cfg.tbls[i].recsz;
            
            fd = open_table_file(i, "DAT", O_RDONLY);
            if (fd == ERROR)
                return I_EOPEN;
            
            nbytes = tsz;
            if (nbytes > I_RECSZ)
            {
                close(fd);
                return I_EREAD;
            }
            
            logical = 0;
            for (phys = 0; phys < g_cfg.tbls[i].maxrec; phys++)
            {
                stats_add(&i_runtime_stats.scan_slots, 1);
                offset = (long)phys * nbytes;
                if (lseek(fd, offset, 0) < 0L)
                {
                    close(fd);
                    return I_EREAD;
                }
                if (read(fd, sbuf, nbytes) != nbytes)
                {
                    close(fd);
                    return I_EREAD;
                }
                
                if (sbuf[0] != I_DELFLAG)
                {
                    if (logical == logidx)
                    {
                        *physidx = phys;
                        close(fd);
                        return I_OK;
                    }
                    logical++;
                }
            }
            
            close(fd);
            return I_ENREC;
}

/* Read record by physical index (bypasses delete check for scanning) */
int i_rdphys(char *tblnam, char *rec, int rnum)
{
    int fd;
    int i, tsz, nbytes;
    long offset;
    
    stats_add(&i_runtime_stats.physical_reads, 1);
    if (rnum < 0)
        return I_ENREC;
    
    i = find_table_index(tblnam);
    if (i < 0)
        return i;
            tsz = g_cfg.tbls[i].recsz;
            if (rnum >= g_cfg.tbls[i].maxrec)
                return I_ENREC;
            
            fd = open_table_file(i, "DAT", O_RDONLY);
            if (fd == ERROR)
                return I_EOPEN;
            
            nbytes = tsz;
            if (nbytes > I_RECSZ)
            {
                close(fd);
                return I_EREAD;
            }
            offset = (long)rnum * nbytes;
            if (lseek(fd, offset, 0) < 0L)
            {
                close(fd);
                return I_EREAD;
            }
            
            /* Read record */
            if (read(fd, rec, nbytes) != nbytes)
            {
                close(fd);
                return I_EREAD;
            }
            
            close(fd);
            
            /* Return special code if deleted */
            if (rec[0] == I_DELFLAG)
                return I_ENREC;
                
            return I_OK;
}

/* Write record by physical index (bypasses logical scan) */
int i_wrphys(char *tblnam, char *rec, int rsiz, int phys)
{
    int fd;
    int i, tsz, nbytes;
    long offset;
    char *oldrec;
    int rc;
    struct persistent_metadata metadata;
    struct persistent_metadata new_metadata;
    
    oldrec = record_workspace;
    if (phys < 0)
        return I_ENREC;
    
    i = find_table_index(tblnam);
    if (i < 0)
        return i;
            rc = load_table_metadata(i, &metadata);
            if (rc != I_OK)
                return rc;
            tsz = g_cfg.tbls[i].recsz;
            if (rsiz != tsz)
                return I_ESIZE;
            if (phys >= g_cfg.tbls[i].maxrec)
                return I_ENREC;
            
            
            fd = open_table_file(i, "DAT", O_RDWR);
            if (fd == ERROR)
                return I_EOPEN;
            
            nbytes = tsz;
            if (nbytes > I_RECSZ)
            {
                close(fd);
                return I_EUPDT;
            }
            offset = (long)phys * nbytes;
            if (lseek(fd, offset, 0) < 0L)
            {
                close(fd);
                return I_EUPDT;
            }
            if (read(fd, oldrec, nbytes) != nbytes)
            {
                close(fd);
                return I_EREAD;
            }
            if (oldrec[0] == I_DELFLAG)
            {
                close(fd);
                return I_ENREC;
            }
            if (lseek(fd, offset, 0) < 0L)
            {
                close(fd);
                return I_EUPDT;
            }
            new_metadata = metadata;
            new_metadata.generation++;
            rc = write_journal(i, JOURNAL_OPERATION_UPDATE, phys, 1,
                oldrec, &metadata);
            if (rc != I_OK)
            {
                close(fd);
                return rc;
            }
            if (simulate_crash_after(I_CRASH_AFTER_JOURNAL))
            {
                close(fd);
                return I_EWRIT;
            }
            if (write(fd, rec, nbytes) != nbytes)
            {
                close(fd);
                return abort_journal(i, I_EUPDT);
            }
            if (fsync(fd) != 0)
            {
                close(fd);
                return abort_journal(i, I_EUPDT);
            }
            if (simulate_crash_after(I_CRASH_AFTER_DATA))
            {
                close(fd);
                return I_EWRIT;
            }
            defer_table_file_invalidation = 1;
            rc = key_update(tblnam, oldrec, rec, phys);
            defer_table_file_invalidation = 0;
            if (rc != I_OK)
            {
                close(fd);
                return abort_journal(i, rc);
            }
            if (simulate_crash_after(I_CRASH_AFTER_DERIVED))
            {
                invalidate_table_files();
                close(fd);
                return I_EWRIT;
            }
            rc = save_table_metadata(i, &new_metadata);
            if (rc != I_OK)
            {
                close(fd);
                return abort_journal(i, rc);
            }
            rc = commit_journal(i);
            if (rc != I_OK)
            {
                close(fd);
                return abort_journal(i, rc);
            }
            if (simulate_crash_after(I_CRASH_AFTER_METADATA))
            {
                close(fd);
                return I_EWRIT;
            }
            rc = clear_journal(i);
            if (rc != I_OK)
            {
                close(fd);
                return rc;
            }
            close(fd);
            return I_OK;
}

/* Delete record by physical slot */
int i_delphys(char *tblnam, int phys)
{
    int fd;
    int i, tsz, nbytes;
    int rc;
    long offset;
    char *sbuf;
    struct persistent_metadata metadata;
    struct persistent_metadata new_metadata;
    
    sbuf = record_workspace;
    if (phys < 0)
        return I_ENREC;
    
    i = find_table_index(tblnam);
    if (i < 0)
        return i;
            rc = load_table_metadata(i, &metadata);
            if (rc != I_OK)
                return rc;
            if (g_cfg.tbls[i].nrecs == 0)
                return I_ENREC;
            if (phys >= g_cfg.tbls[i].maxrec)
                return I_ENREC;
            
            tsz = g_cfg.tbls[i].recsz;
            
            fd = open_table_file(i, "DAT", O_RDWR);
            if (fd == ERROR)
                return I_EOPEN;
            
            nbytes = tsz;
            if (nbytes > I_RECSZ)
            {
                close(fd);
                return I_EUPDT;
            }
            
            offset = (long)phys * nbytes;
            if (lseek(fd, offset, 0) < 0L)
            {
                close(fd);
                return I_EUPDT;
            }
            
            if (read(fd, sbuf, nbytes) != nbytes)
            {
                close(fd);
                return I_EREAD;
            }
            if (sbuf[0] == I_DELFLAG)
            {
                close(fd);
                return I_ENREC;
            }

            memcpy(key_record_workspace, sbuf, nbytes);
            
            new_metadata = metadata;
            new_metadata.active_count--;
            new_metadata.free_head = phys;
            new_metadata.generation++;
            rc = write_journal(i, JOURNAL_OPERATION_DELETE, phys, 1,
                sbuf, &metadata);
            if (rc != I_OK)
            {
                close(fd);
                return rc;
            }
            if (simulate_crash_after(I_CRASH_AFTER_JOURNAL))
            {
                close(fd);
                return I_EWRIT;
            }
            sbuf[0] = I_DELFLAG;
            set_free_link(sbuf, metadata.free_head);
            if (lseek(fd, offset, 0) < 0L)
            {
                close(fd);
                return abort_journal(i, I_EUPDT);
            }
            if (write(fd, sbuf, nbytes) != nbytes)
            {
                close(fd);
                return abort_journal(i, I_EUPDT);
            }
            if (fsync(fd) != 0)
            {
                close(fd);
                return abort_journal(i, I_EUPDT);
            }
            if (simulate_crash_after(I_CRASH_AFTER_DATA))
            {
                close(fd);
                return I_EWRIT;
            }
            defer_table_file_invalidation = 1;
            rc = key_tombstone(i, key_record_workspace, phys);
            defer_table_file_invalidation = 0;
            if (rc != I_OK)
            {
                close(fd);
                return abort_journal(i, rc);
            }
            if (simulate_crash_after(I_CRASH_AFTER_DERIVED))
            {
                invalidate_table_files();
                close(fd);
                return I_EWRIT;
            }
            rc = save_table_metadata(i, &new_metadata);
            if (rc != I_OK)
            {
                close(fd);
                return abort_journal(i, rc);
            }
            rc = commit_journal(i);
            if (rc != I_OK)
            {
                close(fd);
                return abort_journal(i, rc);
            }
            if (simulate_crash_after(I_CRASH_AFTER_METADATA))
            {
                close(fd);
                return I_EWRIT;
            }
            rc = clear_journal(i);
            if (rc != I_OK)
            {
                close(fd);
                return rc;
            }
            close(fd);
            return I_OK;
}

/* Read record by index (0-based) from table data file */
int i_rdrec(char *tblnam, char *rec, int rnum)
{
    int phys;
    
    if (rnum < 0)
        return I_ENREC;
    
    /* Find physical index of logical record */
    if (i_findlog(tblnam, rnum, &phys) != I_OK)
        return I_ENREC;
    return i_rdphys(tblnam, rec, phys);
}

/* Update record by index using temp file rewrite */
int i_uprec(char *tblnam, char *rec, int rsiz, int rnum)
{
    int phys;
    
    if (rnum < 0)
        return I_ENREC;
    
    /* Find physical index of logical record */
    if (i_findlog(tblnam, rnum, &phys) != I_OK)
        return I_ENREC;
    
    return i_wrphys(tblnam, rec, rsiz, phys);
}

/* Delete record by logical index via the indexed physical delete path. */
int i_delrec(char *tblnam, int rnum)
{
    int phys;

    if (rnum < 0)
        return I_ENREC;

    if (i_findlog(tblnam, rnum, &phys) != I_OK)
        return I_ENREC;

    return i_delphys(tblnam, phys);
}

#undef close
