Replace () {
    CheckCommand="grep -ci '$1' Makefile"
    CheckCount=$(eval $CheckCommand)
    if [ "$CheckCount" -gt 0 ]; then
        echo "Requested '$1' to '$2', found - replacing.."
        ReplaceCommand="sed -i 's/$1/$2/g' Makefile"
        eval $ReplaceCommand
    else
        echo "Skipping '$1' to '$2'"
    fi
}

Replace '\-llzma' '\/usr\/lib\/i386\-linux\-gnu\/liblzma\.a'
Replace '\-lXi' '\/usr\/lib\/i386\-linux\-gnu\/libXi\.a \/usr\/lib\/i386\-linux\-gnu\/libXext\.a'
Replace '\-lSM' '\/usr\/lib\/i386\-linux\-gnu\/libSM\.a'
Replace '\-lICE' '\/usr\/lib\/i386\-linux\-gnu\/libICE\.a'
Replace '\-lfontconfig' '\/usr\/lib\/i386\-linux\-gnu\/libfontconfig\.a \/usr\/lib\/i386\-linux\-gnu\/libexpat\.a'
Replace '\-lfreetype' '\/usr\/lib\/i386\-linux\-gnu\/libfreetype\.a'
Replace '\-lXext' '\/usr\/lib\/i386\-linux\-gnu\/libXext\.a'
Replace '\-lopus' '\/usr\/local\/lib\/libopus\.a'
Replace '\-lopenal' '\/usr\/local\/lib\/libopenal\.a'
Replace '\-lavformat' '\/usr\/local\/lib\/libavformat\.a'
Replace '\-lavcodec' '\/usr\/local\/lib\/libavcodec\.a'
Replace '\-lswresample' '\/usr\/local\/lib\/libswresample\.a'
Replace '\-lswscale' '\/usr\/local\/lib\/libswscale\.a'
Replace '\-lavutil' '\/usr\/local\/lib\/libavutil\.a'
Replace '\-lva' '\/usr\/local\/lib\/libva\.a'
Replace '\-lQt5Network' '\/usr\/local\/Qt-5.5.1\/lib\/libQt5Network.a \/usr\/local\/ssl\/lib\/libssl.a \/usr\/local\/ssl\/lib\/libcrypto.a'
