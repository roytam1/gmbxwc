rem Shift-JIS (CP932)
python build_cpbl_csv.py CP932.TXT CP932.DAT -stateless -bin 932 0x3f

rem GBK (CP936)
python build_cpbl_csv.py CP936.TXT CP936.DAT -stateless -bin 936 0x3f

rem EUC-KR (CP949)
python build_cpbl_csv.py CP949.TXT CP949.DAT -stateless -bin 949 0x3f

rem Big5 (UAO 2.50) (CP950)
python build_cpbl_csv.py CP950.TXT CP950.DAT -stateless -bin 950 0x3f

rem Big5-HKSCS-2008 (CP951)
python build_cpbl_csv.py HKSCS2K8.TXT HKSCS2K8.DAT -stateless -bin 951 0x3f

rem GB18030 (CP54936)
python build_cpbl_ucm.py gb18030-2022.ucm GB18030.DAT -gb18030 -bin 54936 0x3f

rem IBM EBCDIC Traditional Chinese Host miexed with 6204 UDC, superset of 5033
python build_cpbl_ucm.py ibm-937_P110-1999.ucm IBM937.DAT -ebcdic -bin 21937 0x6f

rem IBM EBCDIC Japanese Latin Kanji mixed with 4370 UDC, superset of 5035
python build_cpbl_ucm.py ibm-939_P120-1999.ucm IBM939.DAT -ebcdic -bin 21939 0x6f

rem IBM EBCDIC Japanese Katakana-Kanji mixed with 4370 UDC, superset of 5026
python build_cpbl_ucm.py ibm-930_P120-1999.ucm IBM930.DAT -ebcdic -bin 21930 0x6f

rem IBM EBCDIC Korean Mixed with 1880 UDC, superset of 5029
python build_cpbl_ucm.py ibm-933_P110-1995.ucm IBM933.DAT -ebcdic -bin 21933 0x6f

rem IBM EBCDIC Simplified Chinese Host mixed with 1880 UDC, superset of 5031
python build_cpbl_ucm.py ibm-935_P110-1999.ucm IBM935.DAT -ebcdic -bin 21935 0x6f

rem EUC-JP (ICU)
python build_cpbl_ucm.py euc-jp-2007.ucm EUCJP.DAT -stateless -bin 21932 0x3f

rem EUC-TW (ICU)
python build_cpbl_ucm.py euc-tw-2014.ucm EUCTW.DAT -stateless -bin 21950 0x3f

rem legacy NLS CP20000 (Chinese Traditional EUC-TW/CNS-MS)
python build_cpbl_nls.py c_20000.nls c_20000.dat

rem legacy NLS CP20001 (Chinese Traditional TCA)
python build_cpbl_nls.py c_20001.nls c_20001.dat

rem legacy NLS CP20002 (Chinese Traditional ETen)
python build_cpbl_nls.py c_20002.nls c_20002.dat

rem legacy NLS CP20003 (Chinese Traditional IBM5550)
python build_cpbl_nls.py c_20003.nls c_20003.dat

rem legacy NLS CP20004 (Chinese Traditional Teletext)
python build_cpbl_nls.py c_20004.nls c_20004.dat

rem legacy NLS CP20005 (Chinese Traditional Wang)
python build_cpbl_nls.py c_20005.nls c_20005.dat

rem legacy NLS CP20932 (ENC-JP-MS)
python build_cpbl_nls.py c_20932.nls c_20932.dat
