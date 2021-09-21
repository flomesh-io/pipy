import React from 'react';
import Codebase from '../../components/codebase';

import { Match } from '@reach/router';

function Repo() {
  return (
    <Match path="/repo/*">
      {props => props.match ? <Codebase root={'/' + props.match['*']}/> : <div/>}
    </Match>
  );
}

export default Repo;